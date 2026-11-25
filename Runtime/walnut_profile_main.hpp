#ifndef WALNUT_PROFILE_MAIN_HPP
#define WALNUT_PROFILE_MAIN_HPP

#if defined(WALNUT_PROFILE) && (defined(WALNUT_PROFILE_MEMORY) || defined(WALNUT_PROFILE_COMPLEXITY)) && !defined(WALNUT_GC) && !defined(WALNUT_PROFILE_NO_MEMORY)

#include "walnut_profile.hpp"

#include <new>

void* operator new(std::size_t n) {
    void* p = walnut_rt::profile::track_allocate(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t n) {
    void* p = walnut_rt::profile::track_allocate(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return walnut_rt::profile::track_allocate(n ? n : 1); }

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return walnut_rt::profile::track_allocate(n ? n : 1); }

void operator delete(void* p) noexcept { walnut_rt::profile::track_release(p); }

void operator delete[](void* p) noexcept { walnut_rt::profile::track_release(p); }

void operator delete(void* p, std::size_t) noexcept { walnut_rt::profile::track_release(p); }

void operator delete[](void* p, std::size_t) noexcept { walnut_rt::profile::track_release(p); }

void operator delete(void* p, const std::nothrow_t&) noexcept { walnut_rt::profile::track_release(p); }

void operator delete[](void* p, const std::nothrow_t&) noexcept { walnut_rt::profile::track_release(p); }

#endif

#endif // WALNUT_PROFILE_MAIN_HPP
