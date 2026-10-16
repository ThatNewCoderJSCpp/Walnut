# time

`time` has durations, calendar dates, times of day, date-times with and without a UTC offset, clocks, stopwatches, timers, sleeping and waiting, formatting and parsing. It covers what C++'s `std::chrono` does (durations, clocks, `hh_mm_ss`, calendar types, `floor`/`ceil`/`round`, time zones, leap seconds), with names that say what they do. The clocks, sleeping, waiting and text formatting come from Fizmo's time library.

```walnut
import module.time
```

Everything is in the namespace `std::time`. Dates use the proleptic Gregorian calendar, and times are kept to the nanosecond.

## A first example

```walnut
import module.io
import module.time

using std::time::Date;
using std::time::DateTime;
using std::time::Weekday;

function main() -> int {
    Date: leap = Date(2024, 2, 29);
    std::io::println(leap, " is a ", std::time::weekday_name(leap.weekday()), ", day ", leap.day_of_year(), " of the year");
    std::io::println("a year later: ", leap.add_years(1));
    std::io::println("Thanksgiving 2026: ", Date::nth_weekday(2026, 11, Weekday::thursday, 4));

    auto: span = std::time::hours(26) + std::time::minutes(3) + std::time::milliseconds(1500);
    std::io::println(span, " = ", span.to_iso_string(), " = ", span.split());

    DateTime: launch = DateTime(2026, 10, 15, 14, 5, 9);
    std::io::println(launch.format("%A %d %B %Y at %I:%M %p"));
    return 0;
} end function main
```

This prints:

```
2024-02-29 is a Thursday, day 60 of the year
a year later: 2025-02-28
Thanksgiving 2026: 2026-11-26
1d 2h 3m 1.5s = P1DT2H3M1.5S = 26:03:01.5
Thursday 15 October 2026 at 02:05 PM
```

## Duration

A `Duration` is a signed count of nanoseconds, so it reaches about 292 years either way. It is made with the free functions `nanoseconds(n)`, `microseconds(n)`, `milliseconds(n)`, `seconds(n)`, `minutes(n)`, `hours(n)`, `days(n)` and `weeks(n)`; with `from_seconds(x)` and `from_milliseconds(x)` from a `float`; with `from_parts(h, m, s, ns)`; or with `Duration::zero()`, `max()` and `min()`.

| Method | Gives |
|---|---|
| `total_nanoseconds()`, `total_microseconds()`, `total_milliseconds()`, `total_seconds()`, `total_minutes()`, `total_hours()`, `total_days()`, `total_weeks()` | The whole number of each unit, cut toward zero |
| `as_seconds()`, `as_milliseconds()`, `as_microseconds()`, `as_minutes()`, `as_hours()`, `as_days()` | The same as a `float`, with the fraction |
| `split()` | A `HoursMinutesSeconds` (C++'s `hh_mm_ss`) with `negative`, `hours`, `minutes`, `seconds` and `nanoseconds`; its `to_string()` gives `26:03:01.5` |
| `floor_to(unit)`, `ceil_to(unit)`, `round_to(unit)` | Rounded to a multiple of `unit`, such as `seconds(1)`. `round_to` sends halves to the even multiple |
| `abs()`, `is_zero()`, `is_negative()`, `is_positive()` | |
| `ratio(o)` | `this / o` as a `float` |
| `to_string()`, `to_iso_string()` | `1d 2h 3m 1.5s` and the ISO 8601 form `P1DT2H3M1.5S` |

`+`, `-`, unary `-`, `* n`, `n *`, `* x` (a `float`), `/ n`, `%` and the comparisons all work. `a / b` of two durations is how many whole `b` fit in `a`.

## Dates

`Date(year, month, day)` checks that the date exists, and throws a `std::errors::time_error` if it doesn't. The month can also be a `Month`: `Date(2026, Month::october, 15)`.

| Written | Gives |
|---|---|
| `Date::today()`, `Date::today_local()` | Today in UTC, or in the computer's time zone |
| `Date::nth_weekday(year, month, weekday, n)` | The `n`th such weekday of the month, `n` from 1 to 5 |
| `Date::last_weekday(year, month, weekday)` | The last such weekday of the month |
| `Date::from_ordinal(year, day_of_year)` | Day `day_of_year` of the year |
| `Date::from_iso_week(year, week, weekday)` | A date from its ISO week |
| `Date::from_days(n)`, `Date::epoch()` | `n` days after 1970-01-01, and 1970-01-01 itself |
| `Date::parse(text, pattern = "%Y-%m-%d")` | A date read from text; see [Formats](#formats) |

| Method | Gives |
|---|---|
| `year()`, `month()`, `day()`, `month_enum()`, `weekday()` | The parts |
| `day_of_year()`, `quarter()`, `iso_week()` | The day number (1 to 366), the quarter (1 to 4), and an `IsoWeek` with `year`, `week` and `weekday` (`2020-W53-7`) |
| `is_leap_year()`, `days_in_month()`, `days_in_year()`, `is_weekend()` | |
| `first_of_month()`, `last_of_month()`, `first_of_year()`, `last_of_year()` | |
| `add_days(n)`, `add_weeks(n)`, `add_months(n)`, `add_years(n)` | Later (or, with a negative `n`, earlier) dates. Adding months keeps the day when it can and otherwise uses the month's last day, so January 31 plus one month is the last day of February |
| `next(weekday)`, `previous(weekday)` | The next or previous date with that weekday, never the same date |
| `days_until(other)`, `years_until(other)` | Whole days and whole years to another date |
| `days()` | Days since 1970-01-01 |
| `format(pattern)`, `to_string()` | Text; `to_string()` is `2026-10-15` |

`d + duration` and `d - duration` move by whole days, `a - b` is a `Duration`, `++d` and `--d` step a day, and dates compare as expected.

## Times of day

`TimeOfDay(hour, minute)`, `TimeOfDay(hour, minute, second)` and `TimeOfDay(hour, minute, second, nanosecond)` are a time on a 24-hour clock. `TimeOfDay::midnight()`, `noon()`, `from_nanoseconds(n)` and `parse(text, pattern = "%H:%M:%S")` make them too.

It has `hour()`, `minute()`, `second()`, `millisecond()`, `microsecond()`, `nanosecond()`, `hour12()`, `is_am()`, `is_pm()`, `since_midnight()`, `until_midnight()`, `format(pattern)` and `to_string()` (`14:05:09.123`). Adding a `Duration` wraps around midnight.

The free functions `is_am(hour)`, `is_pm(hour)`, `make12(hour)` and `make24(hour12, pm)` convert between the 24-hour and 12-hour clocks.

## Date-times

A `DateTime` is a date and a time of day, with no time zone. Times from the clock are UTC.

| Written | Gives |
|---|---|
| `DateTime(year, month, day)`, and with `hour, minute`, `hour, minute, second`, or `hour, minute, second, nanosecond` | A date-time |
| `DateTime(date, time)` | From a `Date` and a `TimeOfDay` |
| `DateTime::now()`, `DateTime::now_local()` | The current time in UTC, or in the computer's time zone |
| `DateTime::from_unix_seconds(s)`, `from_unix_milliseconds(ms)`, `from_unix_nanoseconds(ns)` | From a Unix time |
| `DateTime::parse(text, pattern = "%Y-%m-%dT%H:%M:%S")` | Read from text. When the pattern has `%z` the result is converted to UTC |
| `DateTime::from_iso_string(text)` | Read from ISO 8601, with or without fractions of a second and an offset (`Z`, `+02:00`) |

It has `date()`, `time()`, every part (`year()`, `month()`, `day()`, `weekday()`, `hour()`, `minute()`, `second()`, `millisecond()`, `nanosecond()`, `day_of_year()`), `unix_seconds()`, `unix_milliseconds()`, `unix_nanoseconds()`, `since_epoch()`, `add_months(n)`, `add_years(n)`, `start_of_day()`, `format(pattern)`, `to_iso_string()` (`2026-10-15T14:05:09.123Z`) and `to_string()` (`2026-10-15 14:05:09.123`). `+` and `-` with a `Duration`, `-` between two date-times, and the comparisons all work.

**Time zones.** `to_local()` treats the date-time as UTC and gives the computer's local time; `to_utc_from_local()` goes the other way. `std::time::local_utc_offset()` and `local_zone_name()` describe the computer's zone.

A `ZonedDateTime` is a UTC `DateTime` together with an offset in seconds and a zone name: `ZonedDateTime(utc, offset_seconds, "IST")`. `ZonedDateTime::now_local()` reads the computer's zone, and `ZonedDateTime::from_local(local, offset, name)` starts from a local time. It has `local()`, `offset()`, `with_offset(seconds, name)`, `offset_string()` (`+05:30`), `format(pattern)`, `to_iso_string()` (`2026-10-15T19:35:09+05:30`) and `to_string()`. Two of them are equal when they are the same instant.

**Leap seconds.** `tai_offset()` and `gps_offset()` give TAI − UTC and GPS − UTC at that moment (37 and 18 seconds since 2017), from the table of leap seconds; `to_tai()` and `to_gps()` add them, and `has_leap_second()` says whether that UTC day ended with a leap second.

## Formats

`format` and `parse` use these fields:

| Field | Means | Field | Means |
|---|---|---|---|
| `%Y` | Year (`2026`) | `%H` | Hour, 00 to 23 |
| `%y` | Year in two digits | `%I` | Hour, 01 to 12 |
| `%C` | Century | `%M` | Minute |
| `%m` | Month, 01 to 12 | `%S` | Second |
| `%d` | Day, 01 to 31 | `%L` | Milliseconds, 3 digits |
| `%e` | Day, padded with a space | `%f` | Microseconds, 6 digits |
| `%j` | Day of the year, 001 to 366 | `%N` | Nanoseconds, 9 digits |
| `%B`, `%b` | Month name (`October`, `Oct`) | `%p`, `%P` | `AM`/`PM`, `am`/`pm` |
| `%A`, `%a` | Weekday name (`Thursday`, `Thu`) | `%z`, `%:z` | UTC offset (`+0530`, `+05:30`) |
| `%u`, `%w` | Weekday number, Monday = 1 or Sunday = 0 | `%Z` | Zone name |
| `%V`, `%G` | ISO week and its year | `%s` | Unix seconds |
| `%q` | Quarter | `%F`, `%T`, `%R`, `%D` | `%Y-%m-%d`, `%H:%M:%S`, `%H:%M`, `%m/%d/%y` |
| `%%` | A `%` | `%n`, `%t` | A newline, a tab |

When parsing, `%f`, `%L` and `%N` all take any number of digits, `%z` also takes `Z`, names match without regard to case, and a space matches any number of spaces.

## Months and weekdays

`Month` is `january` to `december` and `Weekday` is `monday` to `sunday`. `month_number(m)` (1 to 12), `month_of(n)`, `month_name(m)`, `month_short_name(m)`, `weekday_number(w)` (ISO: Monday = 1), `weekday_of(n)`, `weekday_name(w)`, `weekday_short_name(w)`, `is_weekend(w)`, `next_weekday(w)` and `previous_weekday(w)` work with them. `is_leap_year(y)`, `days_in_month(y, m)`, `days_in_year(y)` and `is_valid_date(y, m, d)` work on plain numbers.

## Clocks, sleeping and timers

| Name | Does |
|---|---|
| `Instant::now()` | A reading of the steady clock, which never jumps. An `Instant` has `elapsed()` and `since(earlier)`, and `-` between two gives a `Duration` |
| `sleep(d)`, `sleep_until(instant)` | Lets other programs run for that long |
| `wait(d)` | Waits by sleeping and then spinning, for better timing than `sleep` |
| `precise_wait(d)` | Waits as exactly as the computer allows |
| `yield_now()` | Gives up the rest of this time slice |
| `measure(work)` | How long `work()` took, for a function with no arguments |

```walnut
import module.io
import module.time

function main() -> int {
    auto: watch = std::time::Stopwatch::started();
    std::time::sleep(std::time::milliseconds(20));
    auto: first = watch.lap();
    std::io::println(first.total_milliseconds() >= 20, " ", watch.laps().size());
    return 0;
} end function main
```

This prints `1 1`.

A `Stopwatch` has `start()`, `stop()` (which gives the time so far), `reset()`, `restart()`, `is_running()`, `elapsed()`, `lap()`, `laps()` and `to_string()`. `Stopwatch::started()` makes one that is already running.

A `CountdownTimer(d)` counts down from `d`: `start()`, `pause()`, `reset()`, `reset(d)`, `is_running()`, `elapsed()`, `remaining()`, `expired()`, `progress()` (0 to 1), `duration()` and `wait_until_done()`.

A `RepeatingTimer(period)` ticks every `period`: `ready()` is `true` once per period that has passed, `wait_for_next()` sleeps until the next tick, and `ticks()` and `until_next()` tell where it is. It doesn't try to catch up on ticks it missed.

A `FrameClock` is for loops that run once a frame, as in a game: call `tick()` once a frame to get the time since the last one, and use `delta()`, `delta_seconds()`, `frames()`, `fps()` (smoothed), `total()` and `limit(target_fps)`, which waits out the rest of the frame.
