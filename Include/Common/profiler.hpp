#ifndef WALNUT_PROFILER_HPP
#define WALNUT_PROFILER_HPP

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace walnut {
namespace profiling {

class Profiler {
public:
    struct Entry {
        std::uint64_t calls    = 0;
        std::uint64_t total_ns = 0;
    };

    static Profiler& instance() noexcept {
        static Profiler p;
        return p;
    }

    void record(std::string_view name, std::uint64_t ns) noexcept {
        Entry& e = m_entries[name];   
        ++e.calls;
        e.total_ns += ns;
    }

    void reset() noexcept { m_entries.clear(); }

    void report(std::ostream& os, std::uint64_t total_ns = 0) const;

private:
    std::unordered_map<std::string_view, Entry> m_entries;
};

class ScopeTimer {
public:
    explicit ScopeTimer(std::string_view name) noexcept : m_name(name), m_start(std::chrono::steady_clock::now()) {}

    ~ScopeTimer();

    ScopeTimer(const ScopeTimer&)            = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    std::string_view                      m_name;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace profiling
} // namespace walnut

// Enable by compiling with -DWALNUT_DEBUG

#ifdef WALNUT_DEBUG
    #define WALNUT_PROF_CAT_(a, b) a##b
    #define WALNUT_PROF_CAT(a, b)  WALNUT_PROF_CAT_(a, b)

    #define WALNUT_PROFILE_SCOPE(name) \
        ::walnut::profiling::ScopeTimer WALNUT_PROF_CAT(_walnut_prof_, __LINE__){(name)}

    #define WALNUT_PROFILE_FUNCTION() WALNUT_PROFILE_SCOPE(__func__)

    #define WALNUT_PROFILE_REPORT(os, total_ns) \
        ::walnut::profiling::Profiler::instance().report((os), (total_ns))
    #define WALNUT_PROFILE_RESET() \
        ::walnut::profiling::Profiler::instance().reset()
#else
    #define WALNUT_PROFILE_SCOPE(name)          ((void)0)
    #define WALNUT_PROFILE_FUNCTION()           ((void)0)
    #define WALNUT_PROFILE_REPORT(os, total_ns) ((void)0)
    #define WALNUT_PROFILE_RESET()              ((void)0)
#endif

#endif // WALNUT_PROFILER_HPP