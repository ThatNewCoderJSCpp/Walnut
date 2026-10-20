#ifndef WALNUT_SAFETY_HPP
#define WALNUT_SAFETY_HPP

#include <algorithm>
#include <atomic>
#include <csetjmp>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(WALNUT_CHECK_BOUNDS) || defined(WALNUT_CHECK_NULL) || defined(WALNUT_LEAK_CHECK)
#define WALNUT_TRACK_ALLOCATIONS 1
#endif

#if defined(WALNUT_CHECK_BOUNDS) || defined(WALNUT_CHECK_NULL) || defined(WALNUT_CHECK_OVERFLOW) || defined(WALNUT_LEAK_CHECK) || defined(WALNUT_GC) || defined(WALNUT_CHECK_DANGLING)
#define WALNUT_SAFETY 1
#endif

#if defined(WALNUT_CHECK_DANGLING)
#if defined(_WIN32)
#include <intrin.h>
#else
#include <pthread.h>
#endif
#endif

#if defined(WALNUT_SAFETY)
#define WALNUT_HERE() ::walnut_rt::safety::here(__FILE__, __LINE__)
#else
#define WALNUT_HERE() ((void)0)
#endif

namespace walnut_rt {
namespace safety {

struct site {
    const char* file = nullptr;
    int         line = 0;
};

inline site& current() noexcept {
    thread_local site s;
    return s;
}

inline std::string describe(const site& s) {
    if (!s.file || !*s.file) return std::string();
    std::string f = s.file;
    const std::size_t slash = f.find_last_of("/\\");
    if (slash != std::string::npos) f = f.substr(slash + 1);
    return f + ":" + std::to_string(s.line);
}

inline std::string where_now() {
    const std::string at = describe(current());
    return at.empty() ? std::string() : at + ": ";
}

[[noreturn]] inline void stop(const std::string& message) {
    throw ::walnut_rt::runtime_error(where_now() + message);
}

template <typename T>
struct raw_allocator {
    using value_type = T;
    raw_allocator() noexcept = default;
    template <typename U>
    raw_allocator(const raw_allocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        void* p = std::malloc(n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept { std::free(p); }
    template <typename U>
    bool operator==(const raw_allocator<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const raw_allocator<U>&) const noexcept { return false; }
};

template <typename K, typename V>
using raw_map = std::map<K, V, std::less<K>, raw_allocator<std::pair<const K, V>>>;

struct allocation {
    std::uintptr_t base = 0;
    std::size_t    bytes = 0;
    std::size_t    count = 0;
    std::size_t    element = 0;
    const char*    type = "";
    site           made;
    bool           array = false;
};

struct freed_range {
    std::uintptr_t base = 0;
    std::size_t    bytes = 0;
    const char*    type = "";
    site           made;
    site           freed;
};

enum class release_result {
    ok,
    unknown,
    freed_already,
    inside,
    wrong_form
};

class registry {
public:
    static registry& get() {
        static registry* r = new (std::malloc(sizeof(registry))) registry();
        return *r;
    }

    void add(const void* p, std::size_t element, std::size_t count, const char* type, bool array, site made) {
        if (!p) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        allocation a;
        a.base = reinterpret_cast<std::uintptr_t>(p);
        a.count = count;
        a.element = element;
        a.bytes = element * count;
        a.type = type ? type : "";
        a.made = made;
        a.array = array;
        m_live[a.base] = a;
        m_freed.erase(a.base);
        ++m_allocations;
        m_bytes += a.bytes;
        m_peak = std::max(m_peak, m_bytes);
    }

    release_result release(const void* p, bool array, site freed, allocation& found, freed_range& before) {
        std::lock_guard<std::mutex> lk(m_mutex);
        const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(p);
        const auto it = m_live.find(at);
        if (it == m_live.end()) {
            if (const freed_range* f = freed_containing(at)) {
                before = *f;
                return release_result::freed_already;
            }
            if (const allocation* a = live_containing(at)) {
                found = *a;
                return release_result::inside;
            }
            return release_result::unknown;
        }
        found = it->second;
        if (it->second.array != array) return release_result::wrong_form;
        forget(it, freed);
        return release_result::ok;
    }

    void collected(const void* p) {
        std::lock_guard<std::mutex> lk(m_mutex);
        const auto it = m_live.find(reinterpret_cast<std::uintptr_t>(p));
        if (it != m_live.end()) {
            m_bytes -= it->second.bytes;
            m_live.erase(it);
        }
    }

    bool find(const void* p, allocation& out) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (const allocation* a = live_containing(reinterpret_cast<std::uintptr_t>(p))) {
            out = *a;
            return true;
        }
        return false;
    }

    bool find_freed(const void* p, freed_range& out) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (const freed_range* f = freed_containing(reinterpret_cast<std::uintptr_t>(p))) {
            out = *f;
            return true;
        }
        return false;
    }

    std::vector<allocation> live() {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::vector<allocation> out;
        for (const auto& [base, a] : m_live) out.push_back(a);
        return out;
    }

    std::size_t allocations() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_allocations;
    }

    std::size_t peak() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_peak;
    }

private:
    const allocation* live_containing(std::uintptr_t at) const {
        auto it = m_live.upper_bound(at);
        if (it == m_live.begin()) return nullptr;
        --it;
        const allocation& a = it->second;
        if (at >= a.base && at < a.base + std::max<std::size_t>(a.bytes, 1)) return &a;
        return nullptr;
    }

    const freed_range* freed_containing(std::uintptr_t at) const {
        auto it = m_freed.upper_bound(at);
        if (it == m_freed.begin()) return nullptr;
        --it;
        const freed_range& f = it->second;
        if (at >= f.base && at < f.base + std::max<std::size_t>(f.bytes, 1)) return &f;
        return nullptr;
    }

    void forget(raw_map<std::uintptr_t, allocation>::iterator it, site freed) {
        freed_range f;
        f.base = it->second.base;
        f.bytes = it->second.bytes;
        f.type = it->second.type;
        f.made = it->second.made;
        f.freed = freed;
        m_bytes -= it->second.bytes;
        m_live.erase(it);
        m_freed[f.base] = f;
        m_freed_order.push_back(f.base);
        if (m_freed_order.size() > 131072) {
            const std::size_t drop = m_freed_order.size() / 2;
            for (std::size_t i = 0; i < drop; ++i) m_freed.erase(m_freed_order[i]);
            m_freed_order.erase(m_freed_order.begin(), m_freed_order.begin() + static_cast<std::ptrdiff_t>(drop));
        }
    }

    std::mutex                                                   m_mutex;
    raw_map<std::uintptr_t, allocation>                          m_live;
    raw_map<std::uintptr_t, freed_range>                         m_freed;
    std::vector<std::uintptr_t, raw_allocator<std::uintptr_t>>   m_freed_order;
    std::size_t                                m_allocations = 0;
    std::size_t                                m_bytes = 0;
    std::size_t                                m_peak = 0;
};

#if defined(WALNUT_GC)

namespace gc {

enum : std::uint32_t {
    collectable = 1,
    marked = 2,
    dying = 4,
    destroyed = 8
};

struct block {
    block*          prev;
    block*          next;
    void*           raw;
    std::size_t     size;
    std::uint32_t   flags;
    void*           object;
    std::size_t     count;
    void          (*destroy)(void*, std::size_t);
};

constexpr std::size_t header_size = (sizeof(block) + alignof(std::max_align_t) - 1) / alignof(std::max_align_t) * alignof(std::max_align_t);

struct state {
    std::mutex        mutex;
    block*            head = nullptr;
    std::size_t       blocks = 0;
    std::size_t       bytes = 0;
    std::size_t       since = 0;
    std::size_t       threshold = std::size_t(8) << 20;
    std::size_t       collections = 0;
    std::size_t       freed_total = 0;
    std::atomic<bool> pending{ false };
    std::atomic<int>  threads{ 1 };
    std::atomic<bool> collecting{ false };
    void*             stack_bottom = nullptr;
};

inline state& get() {
    static state* s = new (std::malloc(sizeof(state))) state();
    return *s;
}

inline block*& last_block() noexcept {
    thread_local block* b = nullptr;
    return b;
}

inline void* user_of(block* b) noexcept { return reinterpret_cast<char*>(b) + header_size; }

inline block* block_of(void* user) noexcept { return reinterpret_cast<block*>(reinterpret_cast<char*>(user) - header_size); }

inline void* allocate(std::size_t n, std::size_t align = alignof(std::max_align_t)) {
    if (align <= alignof(std::max_align_t)) {
        void* raw = std::malloc(header_size + (n ? n : 1));
        if (!raw) return nullptr;
        block* b = static_cast<block*>(raw);
        b->raw = raw;
        state& s = get();
        b->size = n;
        b->flags = 0;
        b->object = nullptr;
        b->count = 0;
        b->destroy = nullptr;
        {
            std::lock_guard<std::mutex> lk(s.mutex);
            b->prev = nullptr;
            b->next = s.head;
            if (s.head) s.head->prev = b;
            s.head = b;
            ++s.blocks;
            s.bytes += n;
            s.since += n;
            if (s.since > s.threshold) s.pending = true;
        }
        last_block() = b;
        return user_of(b);
    }
    void* raw = std::malloc(header_size + n + align);
    if (!raw) return nullptr;
    std::uintptr_t user = reinterpret_cast<std::uintptr_t>(raw) + header_size;
    user = (user + align - 1) / align * align;
    block* b = reinterpret_cast<block*>(user - header_size);
    b->raw = raw;
    b->size = n;
    b->flags = 0;
    b->object = nullptr;
    b->count = 0;
    b->destroy = nullptr;
    state& s = get();
    {
        std::lock_guard<std::mutex> lk(s.mutex);
        b->prev = nullptr;
        b->next = s.head;
        if (s.head) s.head->prev = b;
        s.head = b;
        ++s.blocks;
        s.bytes += n;
        s.since += n;
        if (s.since > s.threshold) s.pending = true;
    }
    last_block() = b;
    return reinterpret_cast<void*>(user);
}

inline void unlink(state& s, block* b) noexcept {
    if (b->prev) b->prev->next = b->next;
    else s.head = b->next;
    if (b->next) b->next->prev = b->prev;
    --s.blocks;
    s.bytes -= b->size;
}

inline void release(void* user) noexcept {
    if (!user) return;
    block* b = block_of(user);
    state& s = get();
    {
        std::lock_guard<std::mutex> lk(s.mutex);
        if (b->flags & dying) {
            b->flags |= destroyed;
            return;
        }
        unlink(s, b);
    }
    if (last_block() == b) last_block() = nullptr;
    std::free(b->raw);
}

inline void adopt(void* object, std::size_t count, void (*destroy)(void*, std::size_t)) {
    if (!object) return;
    state& s = get();
    std::lock_guard<std::mutex> lk(s.mutex);
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(object);
    block* b = last_block();
    auto inside = [&](block* c) {
        const std::uintptr_t u = reinterpret_cast<std::uintptr_t>(user_of(c));
        return at >= u && at <= u + c->size;
    };
    if (!b || !inside(b)) {
        b = nullptr;
        for (block* c = s.head; c; c = c->next) {
            if (inside(c)) {
                b = c;
                break;
            }
        }
    }
    if (!b) return;
    b->flags |= collectable;
    b->object = object;
    b->count = count;
    b->destroy = destroy;
}

struct sorted {
    block**     items = nullptr;
    std::size_t size = 0;

    block* find(std::uintptr_t w) const noexcept {
        std::size_t lo = 0;
        std::size_t hi = size;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (reinterpret_cast<std::uintptr_t>(user_of(items[mid])) <= w) lo = mid + 1;
            else hi = mid;
        }
        if (lo == 0) return nullptr;
        block* b = items[lo - 1];
        const std::uintptr_t u = reinterpret_cast<std::uintptr_t>(user_of(b));
        if (w >= u && w <= u + b->size) return b;
        return nullptr;
    }
};

struct worklist {
    block**     items = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;

    void push(block* b) {
        if (size == capacity) {
            capacity = capacity ? capacity * 2 : 1024;
            items = static_cast<block**>(std::realloc(items, capacity * sizeof(block*)));
        }
        items[size++] = b;
    }

    ~worklist() { std::free(items); }
};

inline void scan(const sorted& all, worklist& work, const void* from, const void* to) {
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(from);
    std::uintptr_t e = reinterpret_cast<std::uintptr_t>(to);
    if (a > e) std::swap(a, e);
    a = (a + sizeof(void*) - 1) / sizeof(void*) * sizeof(void*);
    for (; a + sizeof(void*) <= e; a += sizeof(void*)) {
        std::uintptr_t w;
        std::memcpy(&w, reinterpret_cast<const void*>(a), sizeof(w));
        block* b = all.find(w);
        if (!b || (b->flags & marked)) continue;
        b->flags |= marked;
        if (b->flags & collectable) work.push(b);
    }
}

using data_scan_fn = void (*)(const sorted&, worklist&);

inline data_scan_fn& data_scanner() noexcept {
    static data_scan_fn f = nullptr;
    return f;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
inline void scan_stack(const sorted& all, worklist& work, void* bottom) {
    std::jmp_buf registers;
    setjmp(registers);
    volatile char marker = 0;
    const void* top = const_cast<char*>(&marker);
    const void* low = std::min<const void*>(top, static_cast<const void*>(&registers));
    scan(all, work, &registers, reinterpret_cast<const char*>(&registers) + sizeof(registers));
    scan(all, work, low, bottom);
    (void)marker;
}

inline std::size_t collect(bool force = false) {
    state& s = get();
    if (!s.stack_bottom) return 0;
    if (s.threads.load() > 1) {
        s.pending = false;
        std::lock_guard<std::mutex> lk(s.mutex);
        s.since = 0;
        s.threshold = std::max(s.threshold, s.bytes);
        return 0;
    }
    bool expected = false;
    if (!s.collecting.compare_exchange_strong(expected, true)) return 0;
    if (!force && !s.pending) {
        s.collecting = false;
        return 0;
    }
    s.pending = false;
    block** garbage = nullptr;
    std::size_t dead = 0;
    {
        std::lock_guard<std::mutex> lk(s.mutex);
        sorted all;
        all.items = static_cast<block**>(std::malloc(sizeof(block*) * (s.blocks ? s.blocks : 1)));
        for (block* b = s.head; b; b = b->next) {
            b->flags &= ~static_cast<std::uint32_t>(marked);
            all.items[all.size++] = b;
        }
        std::sort(all.items, all.items + all.size, [](block* a, block* b) { return a < b; });
        worklist work;
        scan_stack(all, work, s.stack_bottom);
        if (data_scanner()) data_scanner()(all, work);
        for (std::size_t i = 0; i < all.size; ++i) {
            block* b = all.items[i];
            if (b->flags & collectable) continue;
            scan(all, work, user_of(b), static_cast<char*>(user_of(b)) + b->size);
        }
        while (work.size) {
            block* b = work.items[--work.size];
            scan(all, work, user_of(b), static_cast<char*>(user_of(b)) + b->size);
        }
        garbage = static_cast<block**>(std::malloc(sizeof(block*) * (all.size ? all.size : 1)));
        for (std::size_t i = 0; i < all.size; ++i) {
            block* b = all.items[i];
            if ((b->flags & collectable) && !(b->flags & marked)) {
                b->flags |= dying;
                garbage[dead++] = b;
            }
        }
        std::free(all.items);
    }
    for (std::size_t i = 0; i < dead; ++i) {
        block* b = garbage[i];
        if (b->flags & destroyed) continue;
        b->flags |= destroyed;
#if defined(WALNUT_TRACK_ALLOCATIONS)
        registry::get().collected(b->object);
#endif
        if (b->destroy) b->destroy(b->object, b->count);
    }
    {
        std::lock_guard<std::mutex> lk(s.mutex);
        for (std::size_t i = 0; i < dead; ++i) unlink(s, garbage[i]);
        s.since = 0;
        s.threshold = std::max(std::size_t(8) << 20, s.bytes * 2);
        ++s.collections;
        s.freed_total += dead;
    }
    for (std::size_t i = 0; i < dead; ++i) {
        if (last_block() == garbage[i]) last_block() = nullptr;
        std::free(garbage[i]->raw);
    }
    std::free(garbage);
    s.collecting = false;
    return dead;
}

} // namespace gc

#endif

inline void here(const char* file, int line) noexcept {
    site& s = current();
    s.file = file;
    s.line = line;
    std::atomic_signal_fence(std::memory_order_seq_cst);
#if defined(WALNUT_GC)
    if (gc::get().pending.load(std::memory_order_relaxed)) gc::collect();
#endif
}

struct thread_scope {
#if defined(WALNUT_GC)
    thread_scope() noexcept { ++gc::get().threads; }
    ~thread_scope() { --gc::get().threads; }
#endif
};

inline std::size_t collect_garbage() {
#if defined(WALNUT_GC)
    return gc::collect(true);
#else
    return 0;
#endif
}

[[noreturn]] inline void report(const std::string& message) {
    stop(message);
}

inline std::string made_at(const site& s) {
    const std::string at = describe(s);
    return at.empty() ? std::string() : " (made at " + at + ")";
}

#if defined(WALNUT_CHECK_DANGLING)
struct stack_range {
    std::uintptr_t low = 0;
    std::uintptr_t high = 0;
    bool known = false;
};

inline stack_range find_stack() noexcept {
    stack_range r;
#if defined(_WIN32)
#if defined(_M_X64) || defined(__x86_64__)
    r.high = static_cast<std::uintptr_t>(__readgsqword(0x08));
    r.low = static_cast<std::uintptr_t>(__readgsqword(0x10));
    r.known = r.low && r.high > r.low;
#elif defined(_M_IX86) || defined(__i386__)
    r.high = static_cast<std::uintptr_t>(__readfsdword(0x04));
    r.low = static_cast<std::uintptr_t>(__readfsdword(0x08));
    r.known = r.low && r.high > r.low;
#endif
#elif defined(__APPLE__)
    const std::uintptr_t top = reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(pthread_self()));
    const std::size_t size = pthread_get_stacksize_np(pthread_self());
    r.high = top;
    r.low = top - size;
    r.known = top && size;
#else
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void* addr = nullptr;
        std::size_t size = 0;
        if (pthread_attr_getstack(&attr, &addr, &size) == 0 && addr && size) {
            r.low = reinterpret_cast<std::uintptr_t>(addr);
            r.high = r.low + size;
            r.known = true;
        }
        pthread_attr_destroy(&attr);
    }
#endif
    return r;
}

inline const stack_range& this_stack() noexcept {
    thread_local stack_range r = find_stack();
    return r;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
inline std::uintptr_t stack_now() noexcept {
    volatile char marker = 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(const_cast<char*>(&marker));
    (void)marker;
    return at;
}

inline void check_stack_pointer(const void* p) {
    const stack_range& s = this_stack();
    if (!s.known) return;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(p);
    if (at < s.low || at >= s.high) return;
    const std::uintptr_t now = stack_now();
    if (now < s.low || now >= s.high) return;
    if (at < now) report("this pointer points to a local variable of a function that has already returned, so that variable no longer exists");
}
#endif

template <typename P>
inline void check_pointer(const P& p) {
#if defined(WALNUT_CHECK_DANGLING)
    if constexpr (std::is_pointer_v<P> && std::is_object_v<std::remove_pointer_t<P>>) {
        if (p) check_stack_pointer(const_cast<const void*>(static_cast<const volatile void*>(p)));
    }
#endif
#if defined(WALNUT_CHECK_NULL)
    if constexpr (std::is_pointer_v<P>) {
        if (!p) report("this pointer is null, so there's nothing to reach through it");
#if defined(WALNUT_TRACK_ALLOCATIONS)
        freed_range f;
        if (registry::get().find_freed(p, f)) {
            allocation a;
            if (!registry::get().find(p, a)) {
                const std::string freed = describe(f.freed);
                report(std::string("this pointer is used after its ") + (*f.type ? f.type : "object") + " was deleted" + (freed.empty() ? std::string() : " at " + freed) + made_at(f.made));
            }
        }
#endif
    }
#else
    (void)p;
#endif
}

template <typename P>
inline P&& checked(P&& p) {
    check_pointer(p);
    return std::forward<P>(p);
}

template <typename T>
inline void check_index(T* p, long long i) {
#if defined(WALNUT_CHECK_DANGLING)
    if (p) check_stack_pointer(const_cast<const void*>(static_cast<const volatile void*>(p)));
#endif
#if defined(WALNUT_CHECK_NULL)
    if (!p) report("indexing a null pointer");
#endif
#if defined(WALNUT_CHECK_BOUNDS)
    allocation a;
    if (registry::get().find(p, a) && a.element) {
        const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(p) + static_cast<std::uintptr_t>(i) * sizeof(T);
        if (i < 0 || at + sizeof(T) > a.base + a.bytes || at < a.base) {
            const long long first = static_cast<long long>((reinterpret_cast<std::uintptr_t>(p) - a.base) / sizeof(T));
            report("index " + std::to_string(i) + " is outside the " + std::to_string(a.count) + " " + (*a.type ? std::string(a.type) : std::string("item")) + (a.count == 1 ? "" : "s") + " made with new" + (first ? " (this pointer starts at item " + std::to_string(first) + ")" : std::string()) + made_at(a.made));
        }
    } else {
#if defined(WALNUT_CHECK_NULL) && defined(WALNUT_TRACK_ALLOCATIONS)
        freed_range f;
        if (registry::get().find_freed(p, f)) report("indexing memory that was already deleted at " + describe(f.freed) + made_at(f.made));
#endif
    }
#else
    (void)p;
    (void)i;
#endif
}

template <typename T>
inline void destroy_one(void* p, std::size_t) { static_cast<T*>(p)->~T(); }

template <typename T>
inline void destroy_many(void* p, std::size_t n) {
    T* items = static_cast<T*>(p);
    for (std::size_t i = n; i > 0; --i) items[i - 1].~T();
}

template <typename T>
inline T* track_new(T* p, const char* type, const char* file, int line) {
#if defined(WALNUT_TRACK_ALLOCATIONS)
    registry::get().add(p, sizeof(T), 1, type, false, site{ file, line });
#endif
#if defined(WALNUT_GC)
    gc::adopt(const_cast<std::remove_cv_t<T>*>(p), 1, std::is_trivially_destructible_v<T> ? nullptr : &destroy_one<std::remove_cv_t<T>>);
#endif
    (void)type;
    (void)file;
    (void)line;
    return p;
}

template <typename T>
inline T* track_new_array(T* p, long long count, const char* type, const char* file, int line) {
#if defined(WALNUT_TRACK_ALLOCATIONS)
    registry::get().add(p, sizeof(T), static_cast<std::size_t>(count < 0 ? 0 : count), type, true, site{ file, line });
#endif
#if defined(WALNUT_GC)
    gc::adopt(const_cast<std::remove_cv_t<T>*>(p), static_cast<std::size_t>(count < 0 ? 0 : count), std::is_trivially_destructible_v<T> ? nullptr : &destroy_many<std::remove_cv_t<T>>);
#endif
    (void)count;
    (void)type;
    (void)file;
    (void)line;
    return p;
}

template <typename T>
inline bool before_delete(T* p, bool array, const char* file, int line) {
#if defined(WALNUT_TRACK_ALLOCATIONS)
    if (!p) return true;
    allocation a;
    freed_range f;
    const release_result r = registry::get().release(p, array, site{ file, line }, a, f);
    switch (r) {
    case release_result::ok:
    case release_result::unknown:
        return true;
    case release_result::freed_already: {
        const std::string first = describe(f.freed);
        report(std::string("this ") + (*f.type ? f.type : "object") + " was already deleted" + (first.empty() ? std::string() : " at " + first) + made_at(f.made) + "; deleting it twice corrupts memory");
    }
    case release_result::inside:
        report(std::string("delete needs the pointer that new gave back, but this one points inside a ") + (*a.type ? a.type : "block") + made_at(a.made));
    case release_result::wrong_form:
        report(a.array ? std::string("this was made with new[] (an array), so it needs delete[], not delete") + made_at(a.made) : std::string("this was made with new (one object), so it needs delete, not delete[]") + made_at(a.made));
    }
    return true;
#else
    (void)p;
    (void)array;
    (void)file;
    (void)line;
    return true;
#endif
}

template <typename T>
inline T* new_array(long long count, const char* type, const char* file, int line) {
    if (count < 0) stop("array size " + std::to_string(count) + " is negative");
    T* p = new T[static_cast<std::size_t>(count)]();
    return track_new_array(p, count, type, file, line);
}

template <typename T>
inline void delete_one(T* p, const char* file, int line) {
    before_delete(p, false, file, line);
    delete p;
}

template <typename T>
inline void delete_array(T* p, const char* file, int line) {
    before_delete(p, true, file, line);
    delete[] p;
}

enum class leak_mode {
    off,
    warn,
    error
};

inline leak_mode leak_setting() {
#if defined(WALNUT_LEAK_CHECK)
#if defined(WALNUT_LEAK_ERROR)
    leak_mode mode = leak_mode::error;
#else
    leak_mode mode = leak_mode::warn;
#endif
    if (const char* env = std::getenv("WALNUT_LEAKS")) {
        const std::string v = env;
        if (v == "off" || v == "0") mode = leak_mode::off;
        else if (v == "error") mode = leak_mode::error;
        else if (v == "warn") mode = leak_mode::warn;
    }
    return mode;
#else
    return leak_mode::off;
#endif
}

inline int finish(int code) {
#if defined(WALNUT_LEAK_CHECK)
    const leak_mode mode = leak_setting();
    if (mode == leak_mode::off) return code;
#if defined(WALNUT_GC)
    gc::collect(true);
#endif
    std::vector<allocation> leaks = registry::get().live();
    if (leaks.empty()) return code;
    std::sort(leaks.begin(), leaks.end(), [](const allocation& a, const allocation& b) {
        const std::string fa = describe(a.made);
        const std::string fb = describe(b.made);
        return fa != fb ? fa < fb : a.base < b.base;
    });
    std::size_t bytes = 0;
    for (const allocation& a : leaks) bytes += a.bytes;
    std::fflush(stdout);
    const char* label = mode == leak_mode::error ? "walnut leak error" : "walnut leak warning";
    std::fprintf(stderr, "%s: %zu %s (%zu bytes) made with new %s never deleted\n", label, leaks.size(), leaks.size() == 1 ? "object" : "objects", bytes, leaks.size() == 1 ? "was" : "were");
    std::map<std::string, std::pair<std::size_t, std::size_t>> groups;
    std::vector<std::string> order;
    for (const allocation& a : leaks) {
        const std::string at = describe(a.made);
        const std::string what = std::string("new ") + (*a.type ? a.type : "object") + (a.array ? "[" + std::to_string(a.count) + "]" : std::string());
        const std::string key = (at.empty() ? std::string("somewhere") : at) + ": " + what;
        if (!groups.count(key)) order.push_back(key);
        groups[key].first += 1;
        groups[key].second += a.bytes;
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (i == 50) {
            std::fprintf(stderr, "  ... and %zu more places\n", order.size() - 50);
            break;
        }
        const auto& g = groups[order[i]];
        const std::string line = order[i] + (g.first > 1 ? " (" + std::to_string(g.first) + " times, " + std::to_string(g.second) + " bytes)" : " (" + std::to_string(g.second) + " bytes)");
        std::fprintf(stderr, "  %s\n", line.c_str());
    }
    std::fflush(stderr);
    if (mode == leak_mode::error && code == 0) return 3;
    return code;
#else
    return code;
#endif
}

#if defined(WALNUT_SAFETY) && !defined(_WIN32)
inline void crash_handler(int sig) {
    const site s = current();
    char buffer[512];
    const std::string at = describe(s);
    const char* what = sig == SIGSEGV ? "the program read or wrote memory it doesn't own (a bad pointer)" : sig == SIGFPE ? "an arithmetic error such as dividing by zero" : "the program crashed";
    const int n = std::snprintf(buffer, sizeof buffer, "walnut runtime error: %s%s%s\n", at.c_str(), at.empty() ? "" : ": ", what);
    if (n > 0) {
        std::fflush(stdout);
        std::fwrite(buffer, 1, static_cast<std::size_t>(std::min(n, static_cast<int>(sizeof buffer) - 1)), stderr);
        std::fflush(stderr);
    }
    std::signal(sig, SIG_DFL);
    std::_Exit(70);
}
#endif

inline void start(void* stack_bottom) {
#if defined(WALNUT_GC)
    gc::get().stack_bottom = stack_bottom;
#else
    (void)stack_bottom;
#endif
#if defined(WALNUT_SAFETY) && !defined(_WIN32)
    std::signal(SIGSEGV, crash_handler);
    std::signal(SIGFPE, crash_handler);
#endif
}

} // namespace safety
} // namespace walnut_rt

#endif // WALNUT_SAFETY_HPP
