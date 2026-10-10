#include "Common/arena_allocator.hpp"

namespace walnut {

void* Arena::allocate_raw(std::size_t size, std::size_t align) {
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

bool Arena::try_alloc_current(std::size_t size, std::size_t align) {
    if (blocks_.empty()) return false;
    Block& blk = blocks_.back();
    std::size_t aligned = (blk.used + align - 1) & ~(align - 1);
    return aligned + size <= blk.capacity;
}

void Arena::destroy_all() {
    for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) { it->destroy(it->ptr); }
    destructors_.clear();
}

void Arena::shrink_last(void* ptr, std::size_t old_size, std::size_t new_size) noexcept {
    if (new_size >= old_size || blocks_.empty()) return;
    Block& blk = blocks_.back();
    if (old_size > blk.used) return;
    if (blk.memory + (blk.used - old_size) != static_cast<std::byte*>(ptr)) return;
    blk.used = (blk.used - old_size) + new_size;
}

} // namespace walnut
