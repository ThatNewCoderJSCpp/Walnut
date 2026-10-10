#include "Common/scoped_timer.hpp"

namespace walnut {

ScopedTimer::~ScopedTimer() {
    if (!enabled) return;
    const double ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count() / 1000.0;
    std::cerr << label << ": " << ms << " ms\n";
}

} // namespace walnut
