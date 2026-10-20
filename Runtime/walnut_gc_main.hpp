#ifndef WALNUT_GC_MAIN_HPP
#define WALNUT_GC_MAIN_HPP

#include "walnut_runtime.hpp"

#if defined(WALNUT_GC)

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#else
#include <link.h>
#endif

namespace walnut_rt {
namespace safety {
namespace gc {

inline void platform_scan_data(const sorted& all, worklist& work) {
#if defined(_WIN32)
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const char*>(module) + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
        const char* start = reinterpret_cast<const char*>(module) + sec->VirtualAddress;
        scan(all, work, start, start + sec->Misc.VirtualSize);
    }
#elif defined(__APPLE__)
    const uint32_t images = _dyld_image_count();
    for (uint32_t i = 0; i < images; ++i) {
        const auto* header = reinterpret_cast<const struct mach_header_64*>(_dyld_get_image_header(i));
        if (!header) continue;
        for (const char* section : { "__data", "__bss", "__common" }) {
            unsigned long size = 0;
            const std::uint8_t* start = getsectiondata(header, "__DATA", section, &size);
            if (start && size) scan(all, work, start, start + size);
        }
    }
#else
    struct context {
        const sorted* all;
        worklist*     work;
    } ctx{ &all, &work };
    dl_iterate_phdr([](struct dl_phdr_info* info, std::size_t, void* data) -> int {
        auto* c = static_cast<context*>(data);
        for (int i = 0; i < info->dlpi_phnum; ++i) {
            const auto& ph = info->dlpi_phdr[i];
            if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_W)) continue;
            const char* start = reinterpret_cast<const char*>(info->dlpi_addr + ph.p_vaddr);
            scan(*c->all, *c->work, start, start + ph.p_memsz);
        }
        return 0;
    }, &ctx);
#endif
}

struct install_data_scanner {
    install_data_scanner() noexcept { data_scanner() = &platform_scan_data; }
};

inline install_data_scanner install_data_scanner_instance;

} // namespace gc
} // namespace safety
} // namespace walnut_rt

void* operator new(std::size_t n) {
    void* p = walnut_rt::safety::gc::allocate(n);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t n) {
    void* p = walnut_rt::safety::gc::allocate(n);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return walnut_rt::safety::gc::allocate(n); }

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return walnut_rt::safety::gc::allocate(n); }

void* operator new(std::size_t n, std::align_val_t a) {
    void* p = walnut_rt::safety::gc::allocate(n, static_cast<std::size_t>(a));
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t n, std::align_val_t a) {
    void* p = walnut_rt::safety::gc::allocate(n, static_cast<std::size_t>(a));
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept { walnut_rt::safety::gc::release(p); }

void operator delete[](void* p) noexcept { walnut_rt::safety::gc::release(p); }

void operator delete(void* p, std::size_t) noexcept { walnut_rt::safety::gc::release(p); }

void operator delete[](void* p, std::size_t) noexcept { walnut_rt::safety::gc::release(p); }

void operator delete(void* p, const std::nothrow_t&) noexcept { walnut_rt::safety::gc::release(p); }

void operator delete[](void* p, const std::nothrow_t&) noexcept { walnut_rt::safety::gc::release(p); }

void operator delete(void* p, std::align_val_t) noexcept { walnut_rt::safety::gc::release(p); }

void operator delete[](void* p, std::align_val_t) noexcept { walnut_rt::safety::gc::release(p); }

void operator delete(void* p, std::size_t, std::align_val_t) noexcept { walnut_rt::safety::gc::release(p); }

void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { walnut_rt::safety::gc::release(p); }

#endif

#endif // WALNUT_GC_MAIN_HPP
