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

    void report(std::ostream& os, std::uint64_t total_ns = 0) const {
        if (m_entries.empty()) { os << "[profiler] no samples recorded\n"; return; }
        std::vector<std::pair<std::string_view, Entry>> rows(m_entries.begin(), m_entries.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.total_ns > b.second.total_ns; });
        if (total_ns == 0) { for (const auto& r : rows) total_ns = std::max(total_ns, r.second.total_ns); }
        std::size_t name_w = 5; 
        for (const auto& r : rows) name_w = std::max(name_w, r.first.size());
        const int nw = static_cast<int>(name_w);

        os << std::left  << std::setw(nw) << "Scope"
           << std::right << std::setw(10) << "Calls"
           << std::setw(14) << "Total (us)"
           << std::setw(14) << "Avg (ns)"
           << std::setw(10) << "%" << '\n';

        os << std::string(name_w + 48, '-') << '\n';

        for (const auto& [name, e] : rows) {
            const double total_us = e.total_ns / 1000.0;
            const double avg_ns   = e.calls ? static_cast<double>(e.total_ns) / e.calls : 0.0;
            const double pct      = total_ns ? 100.0 * e.total_ns / total_ns : 0.0;

            os << std::left  << std::setw(nw) << name
               << std::right << std::setw(10) << e.calls
               << std::setw(14) << std::fixed << std::setprecision(3) << total_us
               << std::setw(14) << std::setprecision(1) << avg_ns
               << std::setw(9)  << std::setprecision(2) << pct << '%' << '\n';
        }
    }

private:
    std::unordered_map<std::string_view, Entry> m_entries;
};

class ScopeTimer {
public:
    explicit ScopeTimer(std::string_view name) noexcept : m_name(name), m_start(std::chrono::steady_clock::now()) {}

    ~ScopeTimer() {
        const auto end = std::chrono::steady_clock::now();
        const auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_start).count();
        Profiler::instance().record(m_name, static_cast<std::uint64_t>(ns));
    }

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