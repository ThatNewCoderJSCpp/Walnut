#ifndef WALNUT_TIME_HPP
#define WALNUT_TIME_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../time.hpp"

#include <string>
#include <thread>

namespace walnut_rt {
namespace clock {

namespace fc = fizmo::time::civil;

template <typename... T>
struct tag {};

struct api {};

using Parsed = fc::ParsedCivil;

inline long long now_unix_nanos() { return static_cast<long long>(fc::system_unix_nanoseconds()); }
inline long long steady_nanos() { return static_cast<long long>(fc::steady_nanoseconds()); }
inline long long local_offset(long long unix_seconds) { return static_cast<long long>(fc::local_utc_offset_seconds(unix_seconds)); }
inline std::string zone_name(long long unix_seconds) { return fc::local_zone_name(unix_seconds); }
inline bool sleep_nanos(long long ns) { return fc::sleep_for_nanoseconds(ns); }
inline bool wait_nanos(long long ns) { return fc::wait_for_nanoseconds(ns); }
inline bool precise_wait_nanos(long long ns) { return fc::precise_wait_for_nanoseconds(ns); }
inline void yield_now() { std::this_thread::yield(); }
inline long long tai_minus_utc(long long unix_seconds) { return static_cast<long long>(fc::tai_minus_utc(unix_seconds)); }
inline long long gps_minus_utc(long long unix_seconds) { return static_cast<long long>(fc::gps_minus_utc(unix_seconds)); }
inline bool leap_second_day(long long unix_seconds) { return fc::is_leap_second_day(unix_seconds); }

inline std::string format(const std::string& pattern, long long y, long long m, long long d, long long h, long long mi, long long s, long long ns, long long offset,
                          const std::string& zone) {
    fizmo::time::CivilDate date;
    date.year  = y;
    date.month = static_cast<int>(m);
    date.day   = static_cast<int>(d);
    fizmo::time::CivilTime t;
    t.hour       = static_cast<int>(h);
    t.minute     = static_cast<int>(mi);
    t.second     = static_cast<int>(s);
    t.nanosecond = ns;
    return fc::format(pattern, date, t, static_cast<int>(offset), zone);
}

inline Parsed parse(const std::string& text, const std::string& pattern) { return fc::parse(text, pattern); }

inline bool parsed_ok(const Parsed& p) { return p.ok; }
inline std::string parsed_error(const Parsed& p) { return p.error; }
inline bool parsed_has_offset(const Parsed& p) { return p.has_offset; }

inline long long parsed_field(const Parsed& p, long long which) {
    switch (which) {
        case 0: return static_cast<long long>(p.date.year);
        case 1: return p.date.month;
        case 2: return p.date.day;
        case 3: return p.time.hour;
        case 4: return p.time.minute;
        case 5: return p.time.second;
        case 6: return static_cast<long long>(p.time.nanosecond);
        default: return p.offset_seconds;
    }
}

} // namespace clock
} // namespace walnut_rt

#endif // WALNUT_TIME_HPP
