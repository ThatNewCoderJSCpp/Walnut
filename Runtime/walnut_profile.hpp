#ifndef WALNUT_PROFILE_HPP
#define WALNUT_PROFILE_HPP

#include <atomic>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <map>
#include <type_traits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace walnut_rt {
namespace profile {

inline std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

constexpr std::uint32_t none = 0xffffffffu;

struct site_info {
    const char* file;
    int line;
};

struct function_info {
    const char* name;
    const char* file;
    int line;
    const char* params = "";
};

struct field_info {
    const char* name;
    std::size_t size;
    std::size_t align;
};

struct layout_info {
    const char* name;
    const char* file;
    int line;
    std::size_t size;
    std::size_t align;
    std::vector<field_info> fields;
};

inline std::atomic<std::int64_t> memory_live{ 0 };
inline std::atomic<std::int64_t> memory_peak{ 0 };
inline std::atomic<std::uint64_t> memory_total{ 0 };
inline std::atomic<std::uint64_t> memory_allocs{ 0 };
inline std::atomic<bool> memory_tracked{ false };
inline thread_local bool hook_busy = false;
inline thread_local bool hook_ready = false;

class hook_pause {
public:
    hook_pause() noexcept : m_saved(hook_busy) { hook_busy = true; }
    hook_pause(const hook_pause&) = delete;
    hook_pause& operator=(const hook_pause&) = delete;
    ~hook_pause() { hook_busy = m_saved; }

private:
    bool m_saved;
};

struct growth_cell {
    double n = 0;
    std::uint64_t calls = 0;
    double ns = 0;
    double heap = 0;
    std::uint64_t depth = 0;
};

using growth_table = std::vector<std::map<long long, growth_cell>>;

constexpr std::size_t growth_keys = 512;

inline long long growth_key(double n) {
    if (n <= 64) return static_cast<long long>(n);
    return 1000000 + static_cast<long long>(std::llround(std::log(n) * 40.0));
}

struct counts {
    std::vector<growth_table> growth;
    std::vector<std::uint64_t> site_ns;
    std::vector<std::uint64_t> site_hits;
    std::vector<std::uint64_t> fn_calls;
    std::vector<std::uint64_t> fn_incl;
    std::vector<std::uint64_t> fn_incl_marks;
    std::vector<std::uint64_t> fn_self;
    std::vector<std::uint64_t> site_bytes;
    std::vector<std::uint64_t> site_allocs;
    std::vector<std::int64_t> site_live;
    std::vector<std::int64_t> site_peak;
    std::vector<std::uint64_t> fn_bytes;
    std::vector<std::uint64_t> fn_allocs;
    std::vector<std::uint64_t> fn_incl_bytes;

    void site_room(std::uint32_t i) {
        if (i >= site_ns.size()) {
            hook_pause pause;
            site_ns.resize(i + 1, 0);
            site_hits.resize(i + 1, 0);
        }
    }

    void fn_room(std::uint32_t i) {
        if (i >= fn_calls.size()) {
            hook_pause pause;
            fn_calls.resize(i + 1, 0);
            fn_incl.resize(i + 1, 0);
            fn_incl_marks.resize(i + 1, 0);
            fn_self.resize(i + 1, 0);
        }
    }

    void memory_site_room(std::uint32_t i) {
        if (i >= site_bytes.size()) {
            hook_pause pause;
            site_bytes.resize(i + 1, 0);
            site_allocs.resize(i + 1, 0);
            site_live.resize(i + 1, 0);
            site_peak.resize(i + 1, 0);
        }
    }

    void memory_fn_room(std::uint32_t i) {
        if (i >= fn_bytes.size()) {
            hook_pause pause;
            fn_bytes.resize(i + 1, 0);
            fn_allocs.resize(i + 1, 0);
            fn_incl_bytes.resize(i + 1, 0);
        }
    }

    void add(const counts& o) {
        if (!o.site_ns.empty()) site_room(static_cast<std::uint32_t>(o.site_ns.size() - 1));
        for (std::size_t i = 0; i < o.site_ns.size(); ++i) { site_ns[i] += o.site_ns[i]; site_hits[i] += o.site_hits[i]; }
        if (!o.fn_calls.empty()) fn_room(static_cast<std::uint32_t>(o.fn_calls.size() - 1));
        for (std::size_t i = 0; i < o.fn_calls.size(); ++i) {
            fn_calls[i] += o.fn_calls[i];
            fn_incl[i] += o.fn_incl[i];
            fn_incl_marks[i] += o.fn_incl_marks[i];
            fn_self[i] += o.fn_self[i];
        }
        if (!o.site_bytes.empty()) memory_site_room(static_cast<std::uint32_t>(o.site_bytes.size() - 1));
        for (std::size_t i = 0; i < o.site_bytes.size(); ++i) {
            site_bytes[i] += o.site_bytes[i];
            site_allocs[i] += o.site_allocs[i];
            site_live[i] += o.site_live[i];
            if (o.site_peak[i] > site_peak[i]) site_peak[i] = o.site_peak[i];
        }
        if (!o.fn_bytes.empty()) memory_fn_room(static_cast<std::uint32_t>(o.fn_bytes.size() - 1));
        for (std::size_t i = 0; i < o.fn_bytes.size(); ++i) {
            fn_bytes[i] += o.fn_bytes[i];
            fn_allocs[i] += o.fn_allocs[i];
            fn_incl_bytes[i] += o.fn_incl_bytes[i];
        }
        if (o.growth.size() > growth.size()) growth.resize(o.growth.size());
        for (std::size_t i = 0; i < o.growth.size(); ++i) {
            if (o.growth[i].size() > growth[i].size()) growth[i].resize(o.growth[i].size());
            for (std::size_t k = 0; k < o.growth[i].size(); ++k) {
                for (const auto& [key, cell] : o.growth[i][k]) {
                    growth_cell& mine = growth[i][k][key];
                    mine.n += cell.n;
                    mine.calls += cell.calls;
                    mine.ns += cell.ns;
                    mine.heap += cell.heap;
                    if (cell.depth > mine.depth) mine.depth = cell.depth;
                }
            }
        }
    }
};

struct thread_state {
    counts data;
    std::vector<std::uint32_t> active;
    std::vector<std::uint32_t> deepest;
    std::int64_t live = 0;
    std::int64_t call_peak = 0;
    std::uint32_t current = none;
    std::uint32_t current_fn = none;
    std::uint64_t last = 0;
    std::uint64_t marks = 0;
    std::uint64_t bytes = 0;
    bool real = true;

    thread_state();
    ~thread_state();

    void slice(std::uint64_t t) {
        const std::uint64_t spent = last ? t - last : 0;
        if (current != none) { data.site_room(current); data.site_ns[current] += spent; }
        if (current_fn != none) { data.fn_room(current_fn); data.fn_self[current_fn] += spent; }
        last = t;
    }

    void allocated(std::size_t n) {
        bytes += n;
        live += static_cast<std::int64_t>(n);
        if (live > call_peak) call_peak = live;
        if (current != none) {
            data.memory_site_room(current);
            data.site_bytes[current] += n;
            ++data.site_allocs[current];
            const std::int64_t live = data.site_live[current] += static_cast<std::int64_t>(n);
            if (live > data.site_peak[current]) data.site_peak[current] = live;
        }
        if (current_fn != none) {
            data.memory_fn_room(current_fn);
            data.fn_bytes[current_fn] += n;
            ++data.fn_allocs[current_fn];
        }
    }

    void released(std::uint32_t site, std::size_t n) {
        live -= static_cast<std::int64_t>(n);
        if (site == none) return;
        data.memory_site_room(site);
        data.site_live[site] -= static_cast<std::int64_t>(n);
    }

    void mark(std::uint32_t site) {
        slice(now_ns());
        ++marks;
        data.site_room(site);
        ++data.site_hits[site];
        current = site;
    }
};

class registry {
public:
    std::mutex lock;
    std::vector<site_info> sites;
    std::vector<function_info> functions;
    std::vector<layout_info> layouts;
    counts total;
    std::uint64_t started = now_ns();
    std::uint64_t mark_cost_ps = 0;

    registry() { calibrate(); }

    ~registry() { write(); }

    std::uint32_t add_site(const char* file, int line) {
        std::lock_guard<std::mutex> g(lock);
        sites.push_back(site_info{ file, line });
        return static_cast<std::uint32_t>(sites.size() - 1);
    }

    std::uint32_t add_function(const char* name, const char* file, int line) {
        std::lock_guard<std::mutex> g(lock);
        functions.push_back(function_info{ name, file, line });
        return static_cast<std::uint32_t>(functions.size() - 1);
    }

    void set_params(std::uint32_t id, const char* params) {
        std::lock_guard<std::mutex> g(lock);
        if (id < functions.size()) functions[id].params = params;
    }

    void add_layout(layout_info info) {
        std::lock_guard<std::mutex> g(lock);
        layouts.push_back(std::move(info));
    }

    void merge(const counts& c) {
        std::lock_guard<std::mutex> g(lock);
        total.add(c);
    }

private:
    void calibrate() {
        thread_state probe;
        probe.real = false;
        const int rounds = 5000;
        std::uint64_t best = ~std::uint64_t(0);
        for (int pass = 0; pass < 7; ++pass) {
            const std::uint64_t begin = now_ns();
            for (int i = 0; i < rounds; ++i) probe.mark(0);
            const std::uint64_t spent = now_ns() - begin;
            if (spent < best) best = spent;
        }
        mark_cost_ps = best * 1000 / static_cast<std::uint64_t>(rounds);
    }

    static std::string clean(const char* s) {
        std::string out = s ? s : "";
        for (char& c : out) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        return out;
    }

    void write() {
        hook_pause pause;
        const char* path = std::getenv("WALNUT_PROFILE_OUT");
        std::FILE* f = std::fopen(path && *path ? path : "walnut-profile.tsv", "wb");
        if (!f) return;
        std::lock_guard<std::mutex> g(lock);
        const std::uint64_t wall = now_ns() - started;
        std::fprintf(f, "walnut-profile\t1\n");
        std::fprintf(f, "wall\t%llu\n", static_cast<unsigned long long>(wall));
        std::fprintf(f, "mark-cost-ps\t%llu\n", static_cast<unsigned long long>(mark_cost_ps));
        for (std::size_t i = 0; i < sites.size(); ++i) {
            const std::uint64_t ns = i < total.site_ns.size() ? total.site_ns[i] : 0;
            const std::uint64_t hits = i < total.site_hits.size() ? total.site_hits[i] : 0;
            if (!hits) continue;
            std::fprintf(f, "site\t%s\t%d\t%llu\t%llu\n", clean(sites[i].file).c_str(), sites[i].line, static_cast<unsigned long long>(ns), static_cast<unsigned long long>(hits));
        }
        for (std::size_t i = 0; i < functions.size(); ++i) {
            if (i >= total.fn_calls.size() || !total.fn_calls[i]) continue;
            std::fprintf(f, "function\t%s\t%s\t%d\t%llu\t%llu\t%llu\t%llu\n", clean(functions[i].name).c_str(), clean(functions[i].file).c_str(), functions[i].line,
                static_cast<unsigned long long>(total.fn_calls[i]), static_cast<unsigned long long>(total.fn_incl[i]),
                static_cast<unsigned long long>(total.fn_incl_marks[i]), static_cast<unsigned long long>(total.fn_self[i]));
        }
        if (memory_tracked.load()) {
            const std::int64_t live = memory_live.load();
            std::fprintf(f, "memory\t%llu\t%llu\t%lld\t%lld\n", static_cast<unsigned long long>(memory_total.load()), static_cast<unsigned long long>(memory_allocs.load()),
                static_cast<long long>(memory_peak.load()), static_cast<long long>(live < 0 ? 0 : live));
            for (std::size_t i = 0; i < sites.size() && i < total.site_bytes.size(); ++i) {
                if (!total.site_allocs[i]) continue;
                std::fprintf(f, "msite\t%s\t%d\t%llu\t%llu\t%lld\t%lld\n", clean(sites[i].file).c_str(), sites[i].line, static_cast<unsigned long long>(total.site_bytes[i]),
                    static_cast<unsigned long long>(total.site_allocs[i]), static_cast<long long>(total.site_peak[i]), static_cast<long long>(total.site_live[i] < 0 ? 0 : total.site_live[i]));
            }
            for (std::size_t i = 0; i < functions.size() && i < total.fn_bytes.size(); ++i) {
                if (!total.fn_allocs[i] && !total.fn_incl_bytes[i]) continue;
                std::fprintf(f, "mfunction\t%s\t%s\t%d\t%llu\t%llu\t%llu\n", clean(functions[i].name).c_str(), clean(functions[i].file).c_str(), functions[i].line,
                    static_cast<unsigned long long>(total.fn_bytes[i]), static_cast<unsigned long long>(total.fn_allocs[i]), static_cast<unsigned long long>(total.fn_incl_bytes[i]));
            }
        }
        for (std::size_t i = 0; i < functions.size() && i < total.growth.size(); ++i) {
            std::vector<std::string> names;
            std::string part;
            for (const char* c = functions[i].params; c && *c; ++c) {
                if (*c == ',') { names.push_back(part); part.clear(); }
                else part += *c;
            }
            names.push_back(part);
            for (std::size_t k = 0; k < total.growth[i].size(); ++k) {
                const std::string param = k < names.size() ? names[k] : "argument " + std::to_string(k + 1);
                for (const auto& [key, cell] : total.growth[i][k]) {
                    if (!cell.calls) continue;
                    const double calls = static_cast<double>(cell.calls);
                    std::fprintf(f, "growth\t%s\t%s\t%d\t%s\t%.6g\t%llu\t%.6g\t%.6g\t%llu\n", clean(functions[i].name).c_str(), clean(functions[i].file).c_str(), functions[i].line,
                        clean(param.c_str()).c_str(), cell.n / calls, static_cast<unsigned long long>(cell.calls), cell.ns / calls, cell.heap / calls, static_cast<unsigned long long>(cell.depth));
                }
            }
        }
        for (const layout_info& l : layouts) {
            std::string fields;
            for (const field_info& fi : l.fields) {
                if (!fields.empty()) fields += ',';
                fields += clean(fi.name) + ':' + std::to_string(fi.size) + ':' + std::to_string(fi.align);
            }
            std::fprintf(f, "layout\t%s\t%s\t%d\t%llu\t%llu\t%s\n", clean(l.name).c_str(), clean(l.file).c_str(), l.line, static_cast<unsigned long long>(l.size),
                static_cast<unsigned long long>(l.align), fields.c_str());
        }
        std::fclose(f);
    }
};

inline registry& reg() {
    static registry r;
    return r;
}

inline thread_state::thread_state() : last(now_ns()) {}

inline thread_state::~thread_state() {
    if (!real) return;
    hook_ready = false;
    hook_pause pause;
    slice(now_ns());
    reg().merge(data);
}

inline thread_state& state() {
    reg();
    thread_local thread_state s;
    hook_ready = true;
    return s;
}

struct block_header {
    std::uint64_t size;
    std::uint32_t site;
    std::uint32_t magic;
};

constexpr std::uint32_t block_magic = 0x57414c4eu;

static_assert(sizeof(block_header) == 16, "the profiler's block header has to keep 16-byte alignment");

inline void* track_allocate(std::size_t n) noexcept {
    void* raw = std::malloc(n + sizeof(block_header));
    if (!raw) return nullptr;
    block_header* h = static_cast<block_header*>(raw);
    h->size = n;
    h->site = none;
    h->magic = block_magic;
    memory_tracked.store(true, std::memory_order_relaxed);
    memory_total.fetch_add(n, std::memory_order_relaxed);
    memory_allocs.fetch_add(1, std::memory_order_relaxed);
    const std::int64_t live = memory_live.fetch_add(static_cast<std::int64_t>(n), std::memory_order_relaxed) + static_cast<std::int64_t>(n);
    std::int64_t peak = memory_peak.load(std::memory_order_relaxed);
    while (live > peak && !memory_peak.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {}
    if (!hook_busy && hook_ready) {
        hook_busy = true;
        thread_state& s = state();
        s.allocated(n);
        h->site = s.current;
        hook_busy = false;
    }
    return h + 1;
}

inline void track_release(void* p) noexcept {
    if (!p) return;
    block_header* h = static_cast<block_header*>(p) - 1;
    memory_live.fetch_sub(static_cast<std::int64_t>(h->size), std::memory_order_relaxed);
    if (!hook_busy && hook_ready) {
        hook_busy = true;
        state().released(h->site, static_cast<std::size_t>(h->size));
        hook_busy = false;
    }
    h->magic = 0;
    std::free(h);
}

template <typename T>
constexpr std::size_t field_size() noexcept {
    if constexpr (std::is_reference_v<T>) return sizeof(void*);
    else return sizeof(T);
}

template <typename T>
constexpr std::size_t field_align() noexcept {
    if constexpr (std::is_reference_v<T>) return alignof(void*);
    else return alignof(T);
}

template <typename T>
bool layout(const char* name, const char* file, int line, std::initializer_list<field_info> fields) {
    hook_pause pause;
    reg().add_layout(layout_info{ name, file, line, sizeof(T), alignof(T), std::vector<field_info>(fields) });
    return true;
}

inline std::uint32_t site(const char* file, int line) { return reg().add_site(file, line); }

inline std::uint32_t function_id(const char* name, const char* file, int line) { return reg().add_function(name, file, line); }

inline void mark(std::uint32_t id) { state().mark(id); }

class function_scope {
public:
    explicit function_scope(std::uint32_t id) : m_id(id) {
        thread_state& s = state();
        s.slice(now_ns());
        m_saved_site = s.current;
        m_saved_fn = s.current_fn;
        s.data.fn_room(id);
        ++s.data.fn_calls[id];
        if (id >= s.active.size()) { hook_pause pause; s.active.resize(id + 1, 0); }
        m_outer = s.active[id]++ == 0;
        m_entered = s.last;
        m_entered_marks = s.marks;
        m_entered_bytes = s.bytes;
        s.current_fn = id;
    }

    function_scope(std::uint32_t id, const double* sizes, std::size_t count) : function_scope(id) {
        m_sizes = sizes;
        m_count = count;
        thread_state& s = state();
        if (id >= s.deepest.size()) { hook_pause pause; s.deepest.resize(id + 1, 0); }
        m_saved_deepest = s.deepest[id];
        m_depth_at = s.active[id];
        s.deepest[id] = s.active[id];
        m_live_at = s.live;
        m_saved_peak = s.call_peak;
        s.call_peak = s.live;
    }

    function_scope(const function_scope&) = delete;
    function_scope& operator=(const function_scope&) = delete;

    ~function_scope() {
        thread_state& s = state();
        s.slice(now_ns());
        if (m_sizes) record(s);
        --s.active[m_id];
        if (m_outer) {
            s.data.fn_incl[m_id] += s.last - m_entered;
            s.data.fn_incl_marks[m_id] += s.marks - m_entered_marks;
            if (s.bytes != m_entered_bytes) {
                s.data.memory_fn_room(m_id);
                s.data.fn_incl_bytes[m_id] += s.bytes - m_entered_bytes;
            }
        }
        s.current = m_saved_site;
        s.current_fn = m_saved_fn;
    }

private:
    void record(thread_state& s) {
        hook_pause pause;
        const double ns = static_cast<double>(s.last - m_entered);
        const double heap = s.call_peak > m_live_at ? static_cast<double>(s.call_peak - m_live_at) : 0.0;
        const std::uint32_t depth = s.deepest[m_id] >= m_depth_at ? s.deepest[m_id] - m_depth_at + 1 : 1;
        s.deepest[m_id] = m_saved_deepest > s.deepest[m_id] ? m_saved_deepest : s.deepest[m_id];
        if (s.call_peak < m_saved_peak) s.call_peak = m_saved_peak;
        if (m_id >= s.data.growth.size()) s.data.growth.resize(m_id + 1);
        growth_table& table = s.data.growth[m_id];
        if (table.size() < m_count) table.resize(m_count);
        for (std::size_t i = 0; i < m_count; ++i) {
            const double n = m_sizes[i];
            if (!(n >= 0)) continue;
            const long long key = growth_key(n);
            auto it = table[i].find(key);
            if (it == table[i].end()) {
                if (table[i].size() >= growth_keys) continue;
                it = table[i].emplace(key, growth_cell{}).first;
            }
            growth_cell& c = it->second;
            c.n += n;
            ++c.calls;
            c.ns += ns;
            c.heap += heap;
            if (depth > c.depth) c.depth = depth;
        }
    }

    const double* m_sizes = nullptr;
    std::size_t m_count = 0;
    std::uint32_t m_saved_deepest = 0;
    std::uint32_t m_depth_at = 0;
    std::int64_t m_live_at = 0;
    std::int64_t m_saved_peak = 0;
    std::uint32_t m_id;
    std::uint32_t m_saved_site = none;
    std::uint32_t m_saved_fn = none;
    std::uint64_t m_entered = 0;
    std::uint64_t m_entered_marks = 0;
    std::uint64_t m_entered_bytes = 0;
    bool m_outer = false;
};

template <typename T, typename = void>
struct has_size : std::false_type {};

template <typename T>
struct has_size<T, std::void_t<decltype(std::declval<const T&>().size())>> : std::true_type {};

template <typename T, typename = void>
struct has_length : std::false_type {};

template <typename T>
struct has_length<T, std::void_t<decltype(std::declval<const T&>().length())>> : std::true_type {};

template <typename T, typename = void>
struct has_walnut_size : std::false_type {};

template <typename T>
struct has_walnut_size<T, std::void_t<decltype(std::declval<const T&>().m_size__c())>> : std::true_type {};

template <typename T, typename = void>
struct has_walnut_length : std::false_type {};

template <typename T>
struct has_walnut_length<T, std::void_t<decltype(std::declval<const T&>().m_length__c())>> : std::true_type {};

template <typename T, typename = void>
struct has_to_double : std::false_type {};

template <typename T>
struct has_to_double<T, std::void_t<decltype(std::declval<const T&>().to_double())>> : std::true_type {};

template <typename T, typename = void>
struct as_number : std::false_type {};

template <typename T>
struct as_number<T, std::void_t<decltype(static_cast<double>(std::declval<const T&>()))>> : std::true_type {};

template <typename T>
double size_of(const T& value) {
    using V = std::decay_t<T>;
    if constexpr (std::is_same_v<V, bool> || std::is_pointer_v<V> || std::is_enum_v<V>) return -1;
    else if constexpr (has_walnut_size<V>::value) return size_of(value.m_size__c());
    else if constexpr (has_walnut_length<V>::value) return size_of(value.m_length__c());
    else if constexpr (has_size<V>::value) return size_of(value.size());
    else if constexpr (has_length<V>::value) return size_of(value.length());
    else if constexpr (has_to_double<V>::value) {
        const double n = value.to_double();
        return n >= 0 && n < 1e15 ? n : -1;
    } else if constexpr (as_number<V>::value) {
        const double n = static_cast<double>(value);
        return n >= 0 && n < 1e15 ? n : -1;
    } else return -1;
}

inline std::uint32_t function_id(const char* name, const char* file, int line, const char* params) {
    const std::uint32_t id = reg().add_function(name, file, line);
    reg().set_params(id, params);
    return id;
}

} // namespace profile
} // namespace walnut_rt

#define WALNUT_PROFILE_MARK() do { static const std::uint32_t walnut_profile_site_ = ::walnut_rt::profile::site(__FILE__, __LINE__); ::walnut_rt::profile::mark(walnut_profile_site_); } while (0)
#define WALNUT_PROFILE_FUNCTION(name, line) static const std::uint32_t walnut_profile_fn_id_ = ::walnut_rt::profile::function_id(name, __FILE__, line); ::walnut_rt::profile::function_scope walnut_profile_fn_(walnut_profile_fn_id_)
#if defined(WALNUT_PROFILE_COMPLEXITY)
#define WALNUT_PROFILE_FUNCTION_ARGS(name, line, params, ...) static const std::uint32_t walnut_profile_fn_id_ = ::walnut_rt::profile::function_id(name, __FILE__, line, params); const double walnut_profile_sizes_[] = { __VA_ARGS__ }; ::walnut_rt::profile::function_scope walnut_profile_fn_(walnut_profile_fn_id_, walnut_profile_sizes_, sizeof(walnut_profile_sizes_) / sizeof(double))
#else
#define WALNUT_PROFILE_FUNCTION_ARGS(name, line, params, ...) WALNUT_PROFILE_FUNCTION(name, line)
#endif

#endif // WALNUT_PROFILE_HPP
