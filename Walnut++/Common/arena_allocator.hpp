#ifndef ARENA_HPP
#define ARENA_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <new>
#include <type_traits>

namespace walnut {

class Arena {
public:
    static constexpr std::size_t DEFAULT_BLOCK_SIZE = 64 * 1024; // 64KB blocks
    
private:
    struct Block {
        std::byte* memory;
        std::size_t capacity;
        std::size_t used = 0;
        
        explicit Block(std::size_t size) : memory(new std::byte[size]), capacity(size) {}
        ~Block() { delete[] memory; }
        Block(Block&& o) noexcept : memory(o.memory), capacity(o.capacity), used(o.used) { o.memory = nullptr; }
        Block& operator=(Block&&) = delete;
        Block(const Block&) = delete;
        Block& operator=(const Block&) = delete;
    };
    
    struct DestructorRecord {
        void* ptr;
        void (*destroy)(void*);
    };
    
    std::vector<Block> blocks_;
    std::vector<DestructorRecord> destructors_;
    std::size_t block_size_;
    
    void* allocate_raw(std::size_t size, std::size_t align) {
        if (blocks_.empty() || !try_alloc_current(size, align)) {
            std::size_t new_size = std::max(block_size_, size + align);
            blocks_.emplace_back(new_size);
        }
        
        Block& blk = blocks_.back();
        std::size_t aligned = (blk.used + align - 1) & ~(align - 1);
        void* result = blk.memory + aligned;
        blk.used = aligned + size;
        return result;
    }
    
    bool try_alloc_current(std::size_t size, std::size_t align) {
        if (blocks_.empty()) return false;
        Block& blk = blocks_.back();
        std::size_t aligned = (blk.used + align - 1) & ~(align - 1);
        return aligned + size <= blk.capacity;
    }
    
public:
    explicit Arena(std::size_t block_size = DEFAULT_BLOCK_SIZE) : block_size_(block_size) {
        blocks_.reserve(16);
        destructors_.reserve(512);
    }
    
    ~Arena() { destroy_all(); }
    
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) = default;
    Arena& operator=(Arena&&) = default;
    
    template<typename T, typename... Args>
    T* alloc(Args&&... args) {
        void* mem = allocate_raw(sizeof(T), alignof(T));
        T* obj = ::new (mem) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>) { destructors_.push_back({obj, [](void* p) { static_cast<T*>(p)->~T(); }}); }
        return obj;
    }
    
    void* alloc_raw(std::size_t size, std::size_t align = alignof(std::max_align_t)) {
        return allocate_raw(size, align);
    }
    
    template<typename T>
    T* alloc_array(std::size_t count) {
        if (count == 0) return nullptr;
        void* mem = allocate_raw(sizeof(T) * count, alignof(T));
        T* arr = static_cast<T*>(mem);
        
        for (std::size_t i = 0; i < count; ++i) {
            ::new (arr + i) T();
            if constexpr (!std::is_trivially_destructible_v<T>) {
                destructors_.push_back({arr + i, [](void* p) { static_cast<T*>(p)->~T(); }});
            }
        }
        return arr;
    }

    template<typename T>
    T* alloc_array_uninit(std::size_t count) {
        if (count == 0) return nullptr;
        return static_cast<T*>(allocate_raw(sizeof(T) * count, alignof(T)));
    }
    
    void destroy_all() {
        for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) { it->destroy(it->ptr); }
        destructors_.clear();
    }
    
    void reset() {
        destroy_all();
        for (auto& blk : blocks_) blk.used = 0;
    }

    void shrink_last(void* ptr, std::size_t old_size, std::size_t new_size) noexcept {
        if (new_size >= old_size || blocks_.empty()) return;
        Block& blk = blocks_.back();
        if (old_size > blk.used) return;
        if (blk.memory + (blk.used - old_size) != static_cast<std::byte*>(ptr)) return;
        blk.used = (blk.used - old_size) + new_size;
    }

    template<typename T>
    void shrink_array_last(T* ptr, std::size_t old_count, std::size_t new_count) noexcept {
        shrink_last(ptr, old_count * sizeof(T), new_count * sizeof(T));
    }
    
    std::size_t bytes_used() const {
        std::size_t total = 0;
        for (const auto& blk : blocks_) total += blk.used;
        return total;
    }
    
    std::size_t bytes_allocated() const {
        std::size_t total = 0;
        for (const auto& blk : blocks_) total += blk.capacity;
        return total;
    }
    
    std::size_t block_count() const { return blocks_.size(); }
};

template<typename T>
class ArenaPtr {
    T* ptr_ = nullptr;
public:
    ArenaPtr() = default;
    explicit ArenaPtr(T* p) : ptr_(p) {}
    ArenaPtr(std::nullptr_t) : ptr_(nullptr) {}
    ArenaPtr(ArenaPtr&& o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }
    ArenaPtr& operator=(ArenaPtr&& o) noexcept { ptr_ = o.ptr_; o.ptr_ = nullptr; return *this; }
    ArenaPtr& operator=(std::nullptr_t) noexcept { ptr_ = nullptr; return *this; }
    ArenaPtr(const ArenaPtr&) = delete;
    ArenaPtr& operator=(const ArenaPtr&) = delete;
    
public:
    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    T* release() noexcept { T* p = ptr_; ptr_ = nullptr; return p; }
    void reset(T* p = nullptr) noexcept { ptr_ = p; }
};

template<typename T, typename... Args>
ArenaPtr<T> make_arena(Arena& arena, Args&&... args) {
    return ArenaPtr<T>(arena.alloc<T>(std::forward<Args>(args)...));
}

template<typename T, typename... Args>
T* make_in(Arena& arena, Args&&... args) {
    return arena.alloc<T>(std::forward<Args>(args)...);
}

} // namespace walnut

#endif // ARENA_HPP