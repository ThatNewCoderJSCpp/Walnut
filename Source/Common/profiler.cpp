#include "Common/profiler.hpp"

namespace walnut {
namespace profiling {

void Profiler::report(std::ostream& os, std::uint64_t total_ns) const {
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

ScopeTimer::~ScopeTimer() {
    const auto end = std::chrono::steady_clock::now();
    const auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_start).count();
    Profiler::instance().record(m_name, static_cast<std::uint64_t>(ns));
}

} // namespace profiling
} // namespace walnut
