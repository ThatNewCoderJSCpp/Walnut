#ifndef WALNUT_SCOPED_TIMER_HPP
#define WALNUT_SCOPED_TIMER_HPP

#include <chrono>
#include <iostream>

namespace walnut {

struct ScopedTimer {
    const char* label;
    bool        enabled;
    std::chrono::high_resolution_clock::time_point start;

    explicit ScopedTimer(const char* l, bool on = true) : label(l), enabled(on), start(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

} // namespace walnut

#endif // WALNUT_SCOPED_TIMER_HPP