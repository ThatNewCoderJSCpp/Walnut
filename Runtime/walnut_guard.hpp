#ifndef WALNUT_GUARD_HPP
#define WALNUT_GUARD_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../networking.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace guard {

namespace fg = fizmo::networking::guard;

template <typename Fn>
inline auto guarded(Fn&& fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const runtime_error&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

template <typename F>
struct callable_traits : callable_traits<decltype(&F::operator())> {};

template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...) const> {
    using result = R;
    using args   = std::tuple<A...>;
};

template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...)> {
    using result = R;
    using args   = std::tuple<A...>;
};

template <typename R, typename... A>
struct callable_traits<std::function<R(A...)>> {
    using result = R;
    using args   = std::tuple<A...>;
};

template <typename F, std::size_t I>
using arg_of = std::decay_t<std::tuple_element_t<I, typename callable_traits<std::decay_t<F>>::args>>;

template <typename T, typename V>
inline T to_walnut(V&& v) {
    if constexpr (std::is_same_v<std::decay_t<V>, T>) return T(std::forward<V>(v));
    else if constexpr (std::is_constructible_v<T, V&&>) return T(std::forward<V>(v));
    else return T(walnut_rt::native_tag{}, std::forward<V>(v));
}

struct api {};

inline long long ms_of(std::chrono::milliseconds ms) { return static_cast<long long>(ms.count()); }

class rate_decision {
public:
    rate_decision() = default;
    explicit rate_decision(const fg::RateLimitDecision& d) : m_d(d) {}

    bool allowed() const { return m_d.allowed; }
    double remaining() const { return m_d.remaining; }
    double limit() const { return m_d.limit; }
    long long retry_after_ms() const { return ms_of(m_d.retry_after); }
    long long reset_after_ms() const { return ms_of(m_d.reset_after); }

private:
    fg::RateLimitDecision m_d;
};

class rate_limiter {
public:
    rate_limiter() : rate_limiter(1.0, 60.0) {}

    rate_limiter(double per_second, double burst, double start_tokens = -1.0) {
        fg::RateLimitPolicy p = fg::RateLimitPolicy::per_second(per_second, burst);
        p.initial_tokens = start_tokens;
        m_l = std::make_shared<fg::RateLimiter>(p);
    }

    static rate_limiter per_minute(double per_minute, double burst) { return rate_limiter(per_minute / 60.0, burst); }
    static rate_limiter from_wide(const walnut_rt::f128& per_second, const walnut_rt::f128& burst) { return rate_limiter(walnut_rt::to_native<double>(per_second), walnut_rt::to_native<double>(burst)); }
    static rate_limiter from_wide_start(const walnut_rt::f128& per_second, const walnut_rt::f128& burst, const walnut_rt::f128& start_tokens) { return rate_limiter(walnut_rt::to_native<double>(per_second), walnut_rt::to_native<double>(burst), walnut_rt::to_native<double>(start_tokens)); }
    static rate_limiter per_hour(double per_hour, double burst) { return rate_limiter(per_hour / 3600.0, burst); }

    rate_decision consume(const std::string& key, double cost = 1.0) { return rate_decision(m_l->consume(key, cost)); }
    rate_decision peek(const std::string& key, double cost = 1.0) const { return rate_decision(m_l->peek(key, cost)); }
    bool allow(const std::string& key) { return m_l->consume(key).allowed; }
    void refund(const std::string& key, double amount = 1.0) { m_l->refund(key, amount); }
    void reset(const std::string& key) { m_l->reset(key); }
    long long size() const { return static_cast<long long>(m_l->size()); }
    long long sweep() { return static_cast<long long>(m_l->sweep()); }

private:
    std::shared_ptr<fg::RateLimiter> m_l;
};

class window_limiter {
public:
    window_limiter() : window_limiter(10, 60) {}

    window_limiter(long long limit, long long window_seconds) {
        fg::WindowPolicy p;
        p.limit = static_cast<std::uint32_t>(limit < 1 ? 1 : limit);
        p.window = std::chrono::seconds(window_seconds < 1 ? 1 : window_seconds);
        m_l = std::make_shared<fg::SlidingWindowLimiter>(p);
    }

    rate_decision hit(const std::string& key) { return rate_decision(m_l->hit(key)); }
    rate_decision peek(const std::string& key) const { return rate_decision(m_l->peek(key)); }
    long long count(const std::string& key) const { return static_cast<long long>(m_l->count(key)); }
    void reset(const std::string& key) { m_l->reset(key); }

private:
    std::shared_ptr<fg::SlidingWindowLimiter> m_l;
};

class failure_status {
public:
    failure_status() = default;
    explicit failure_status(const fg::FailureStatus& s) : m_s(s) {}

    long long failures() const { return m_s.failures; }
    long long successes() const { return m_s.successes; }
    double failure_ratio() const { return m_s.failure_ratio; }
    bool locked() const { return m_s.locked; }
    long long strikes() const { return m_s.strikes; }
    long long lock_remaining_ms() const { return ms_of(m_s.lock_remaining); }
    long long delay_ms() const { return ms_of(m_s.suggested_delay); }

private:
    fg::FailureStatus m_s;
};

class failure_tracker {
public:
    failure_tracker() : failure_tracker(10, 900, 60) {}

    failure_tracker(long long max_failures, long long window_seconds, long long lockout_seconds) {
        fg::FailurePolicy p;
        p.max_failures = static_cast<std::uint32_t>(max_failures < 1 ? 1 : max_failures);
        p.window = std::chrono::seconds(window_seconds < 1 ? 1 : window_seconds);
        p.lockout = std::chrono::seconds(lockout_seconds < 1 ? 1 : lockout_seconds);
        m_t = std::make_shared<fg::FailureTracker>(p);
    }

    failure_status record_failure(const std::string& key) { return failure_status(m_t->record_failure(key)); }
    failure_status record_success(const std::string& key, bool forgive = false) { return failure_status(m_t->record_success(key, forgive)); }
    failure_status status(const std::string& key) const { return failure_status(m_t->status(key)); }
    bool is_locked(const std::string& key) const { return m_t->is_locked(key); }
    void unlock(const std::string& key) { m_t->unlock(key); }
    void reset(const std::string& key) { m_t->reset(key); }

private:
    std::shared_ptr<fg::FailureTracker> m_t;
};

class log_fields {
public:
    log_fields() = default;

    log_fields& add(const std::string& key, const std::string& value) { m_f.push_back(fg::field(key, value)); return *this; }
    log_fields& add_int(const std::string& key, long long value) { m_f.push_back(fg::field(key, static_cast<std::int64_t>(value))); return *this; }
    log_fields& add_float(const std::string& key, double value) { m_f.push_back(fg::field(key, value)); return *this; }
    log_fields& add_bool(const std::string& key, bool value) { m_f.push_back(fg::field(key, value)); return *this; }
    long long size() const { return static_cast<long long>(m_f.size()); }

    const fg::LogFields& raw() const noexcept { return m_f; }

private:
    fg::LogFields m_f;
};

inline fg::LogLevel level_of(const std::string& name) {
    fg::LogLevel l;
    if (!fg::parse_log_level(name, l)) fail("log level must be trace, debug, info, warn, error, fatal or off, not \"" + name + "\"");
    return l;
}

class logger {
public:
    logger() : m_log(std::make_shared<fg::Logger>(fg::LogLevel::Info)) {}
    explicit logger(const std::string& level) : m_log(std::make_shared<fg::Logger>(level_of(level))) {}

    void to_stderr() { m_log->add_sink(std::make_shared<fg::StreamSink>(stderr)); }
    void to_stdout() { m_log->add_sink(std::make_shared<fg::StreamSink>(stdout)); }

    void to_file(const std::string& path, long long max_bytes = 10 << 20, long long files = 5) {
        auto sink = std::make_shared<fg::FileSink>(path, static_cast<std::size_t>(max_bytes < 0 ? 0 : max_bytes), static_cast<std::size_t>(files < 1 ? 1 : files));
        if (!sink->good()) fail("can't open log file '" + path + "'");
        m_log->add_sink(sink);
    }

    void as_text(bool text = true) { m_log->set_format(text ? fg::LogFormat::Text : fg::LogFormat::Json); }
    void set_level(const std::string& level) { m_log->set_level(level_of(level)); }
    void redact(const std::string& key) { m_log->add_redacted_key(key); }

    void trace(const std::string& msg) { m_log->trace(msg); }
    void debug(const std::string& msg) { m_log->debug(msg); }
    void info(const std::string& msg) { m_log->info(msg); }
    void warn(const std::string& msg) { m_log->warn(msg); }
    void error(const std::string& msg) { m_log->error(msg); }
    void info_with(const std::string& msg, const log_fields& f) { m_log->info(msg, f.raw()); }
    void warn_with(const std::string& msg, const log_fields& f) { m_log->warn(msg, f.raw()); }
    void error_with(const std::string& msg, const log_fields& f) { m_log->error(msg, f.raw()); }
    void flush() { m_log->flush(); }

    static std::string new_request_id() { return fg::Logger::new_request_id(); }

    const std::shared_ptr<fg::Logger>& raw() const noexcept { return m_log; }

private:
    std::shared_ptr<fg::Logger> m_log;
};

class job {
public:
    job() = default;
    explicit job(const fg::JobContext& c) : m_id(c.id), m_name(c.name), m_payload(c.payload), m_attempt(c.attempt), m_max_attempts(c.max_attempts), m_last_error(c.last_error), m_stopping(c.stopping) {}

    std::string id() const { return m_id; }
    std::string name() const { return m_name; }
    std::string payload() const { return m_payload; }
    long long attempt() const { return m_attempt; }
    long long max_attempts() const { return m_max_attempts; }
    bool last_attempt() const { return m_attempt >= m_max_attempts; }
    std::string last_error() const { return m_last_error; }
    bool should_stop() const { return m_stopping && m_stopping->load(); }

private:
    std::string              m_id, m_name, m_payload;
    long long                m_attempt = 0;
    long long                m_max_attempts = 0;
    std::string              m_last_error;
    const std::atomic<bool>* m_stopping = nullptr;
};

inline std::vector<std::string> issue_texts(const std::vector<fg::SchemaIssue>& issues) {
    std::vector<std::string> out;
    out.reserve(issues.size());
    for (const auto& i : issues) out.push_back(i.to_string());
    return out;
}

inline fg::StringFormat format_named(const std::string& name) {
    if (name == "identifier") return fg::StringFormat::Identifier;
    if (name == "email") return fg::StringFormat::Email;
    if (name == "uuid") return fg::StringFormat::Uuid;
    if (name == "url") return fg::StringFormat::Url;
    if (name == "hex") return fg::StringFormat::Hex;
    if (name == "base64") return fg::StringFormat::Base64;
    if (name == "digits") return fg::StringFormat::Digits;
    if (name == "date") return fg::StringFormat::Date;
    if (name == "datetime") return fg::StringFormat::DateTime;
    if (name.empty() || name == "none") return fg::StringFormat::None;
    fail("text format must be identifier, email, uuid, url, hex, base64, digits, date, datetime or none, not \"" + name + "\"");
}

inline std::size_t count_of_size(long long n, const char* what) {
    if (n < 0) fail(std::string(what) + " can't be negative");
    return static_cast<std::size_t>(n);
}

class job_schema;

class field_rule {
public:
    field_rule() = default;
    explicit field_rule(fg::FieldRule r) : m_r(std::move(r)) {}

    static field_rule text_field() { return field_rule(fg::FieldRule::string()); }
    static field_rule int_field() { return field_rule(fg::FieldRule::integer()); }
    static field_rule float_field() { return field_rule(fg::FieldRule::number()); }
    static field_rule bool_field() { return field_rule(fg::FieldRule::boolean()); }
    static field_rule any_field() { return field_rule(fg::FieldRule::any()); }
    static field_rule list_of(const field_rule& items) { return field_rule(fg::FieldRule::array(items.m_r)); }
    static field_rule object_of(const job_schema& fields);

    field_rule& optional() { m_r.optional(); return *this; }
    field_rule& required() { m_r.required(true); return *this; }
    field_rule& nullable() { m_r.nullable(true); return *this; }
    field_rule& min(double v) { m_r.min(v); return *this; }
    field_rule& max(double v) { m_r.max(v); return *this; }
    field_rule& between(double lo, double hi) { if (lo > hi) fail("between needs low <= high"); m_r.between(lo, hi); return *this; }
    field_rule& positive() { m_r.positive(); return *this; }
    field_rule& min_length(long long n) { m_r.min_length(count_of_size(n, "min_length")); return *this; }
    field_rule& max_length(long long n) { m_r.max_length(count_of_size(n, "max_length")); return *this; }
    field_rule& length(long long lo, long long hi) { if (lo > hi) fail("length needs low <= high"); m_r.length(count_of_size(lo, "length"), count_of_size(hi, "length")); return *this; }
    field_rule& non_empty() { m_r.non_empty(); return *this; }
    field_rule& min_items(long long n) { m_r.min_items(count_of_size(n, "min_items")); return *this; }
    field_rule& max_items(long long n) { m_r.max_items(count_of_size(n, "max_items")); return *this; }
    field_rule& unique_items() { m_r.unique_items(true); return *this; }
    field_rule& allow_control_chars() { m_r.allow_control_chars(true); return *this; }
    field_rule& format(const std::string& name) { m_r.format(format_named(name)); return *this; }

    template <typename V>
    field_rule& one_of(const V& values) {
        std::vector<fizmo::json::Value> list;
        for (const auto& v : values) list.emplace_back(std::string(v));
        m_r.one_of(std::move(list));
        return *this;
    }

    const fg::FieldRule& native() const noexcept { return m_r; }

private:
    fg::FieldRule m_r = fg::FieldRule::any();
};

class job_schema {
public:
    job_schema() = default;
    explicit job_schema(fg::JobSchema s) : m_s(std::move(s)) {}

    static job_schema text_payload(long long max_bytes = 64 * 1024) { return job_schema(fg::JobSchema::text(count_of_size(max_bytes, "max_bytes"))); }
    static job_schema no_payload() { return job_schema(fg::JobSchema::empty()); }

    job_schema& add(const std::string& name, const field_rule& rule) { m_s.field(name, rule.native()); return *this; }
    job_schema& allow_extra_fields() { m_s.allow_extra_fields(true); return *this; }
    job_schema& max_bytes(long long n) { m_s.max_bytes(count_of_size(n, "max_bytes")); return *this; }
    job_schema& max_depth(long long n) { m_s.max_depth(count_of_size(n, "max_depth")); return *this; }
    job_schema& max_string_length(long long n) { m_s.max_string_length(count_of_size(n, "max_string_length")); return *this; }
    job_schema& max_array_items(long long n) { m_s.max_array_items(count_of_size(n, "max_array_items")); return *this; }

    template <typename V>
    job_schema& at_least_one_of(const V& names) { m_s.at_least_one_of(std::vector<std::string>(names.begin(), names.end())); return *this; }

    template <typename V>
    job_schema& only_one_of(const V& names) { m_s.only_one_of(std::vector<std::string>(names.begin(), names.end())); return *this; }

    template <typename V>
    job_schema& all_or_none_of(const V& names) { m_s.all_or_none_of(std::vector<std::string>(names.begin(), names.end())); return *this; }

    std::vector<std::string> validate(const std::string& payload) const { return issue_texts(m_s.validate(payload)); }
    bool accepts(const std::string& payload) const { return m_s.accepts(payload); }
    field_rule as_field() const { return field_rule(fg::FieldRule::object(m_s)); }

    const fg::JobSchema& native() const noexcept { return m_s; }

private:
    fg::JobSchema m_s;
};

inline field_rule field_rule::object_of(const job_schema& fields) {
    return field_rule(fg::FieldRule::object(fields.native()));
}

class enqueue_result {
public:
    enqueue_result() = default;
    explicit enqueue_result(const fg::EnqueueResult& r) : m_ok(r.ok), m_duplicate(r.duplicate), m_id(r.id), m_problems(issue_texts(r.issues)) {}

    bool ok() const { return m_ok; }
    bool duplicate() const { return m_duplicate; }
    std::string id() const { return m_id; }
    std::vector<std::string> problems() const { return m_problems; }
    std::string message() const {
        std::string out;
        for (std::size_t i = 0; i < m_problems.size(); ++i) out += (i ? "; " : "") + m_problems[i];
        return out;
    }

private:
    bool                     m_ok = false;
    bool                     m_duplicate = false;
    std::string              m_id;
    std::vector<std::string> m_problems;
};

class job_queue {
public:
    job_queue() : job_queue(1) {}
    explicit job_queue(long long workers) : m_q(std::make_shared<fg::JobQueue>(static_cast<std::size_t>(workers < 1 ? 1 : workers))) {}

    template <typename F>
    void on(const std::string& name, F handler) {
        guarded([&] { m_q->register_handler(name, wrap(handler)); });
    }

    template <typename F>
    void on_checked(const std::string& name, const job_schema& schema, F handler) {
        guarded([&] { m_q->register_handler(name, wrap(handler), schema.native()); });
    }

    template <typename F>
    static fg::JobHandler wrap(F handler) {
        return [handler](const fg::JobContext& ctx) mutable {
            try {
                const bool ok = handler(to_walnut<arg_of<F, 0>>(job(ctx)));
                return ok ? fg::JobResult::ok() : fg::JobResult::retry("the handler returned false");
            } catch (const std::exception& e) {
                return fg::JobResult::retry(e.what());
            } catch (...) {
                return fg::JobResult::retry("the handler threw");
            }
        };
    }

    std::string enqueue(const std::string& name, const std::string& payload = "", long long max_attempts = 5, long long delay_ms = 0, const std::string& unique_key = "") {
        const fg::JobOptions o = options(max_attempts, delay_ms, unique_key);
        return guarded([&] { return m_q->enqueue(name, payload, o); });
    }

    enqueue_result try_enqueue(const std::string& name, const std::string& payload = "", long long max_attempts = 5, long long delay_ms = 0, const std::string& unique_key = "") {
        const fg::JobOptions o = options(max_attempts, delay_ms, unique_key);
        return enqueue_result(m_q->try_enqueue(name, payload, o));
    }

    std::vector<std::string> check(const std::string& name, const std::string& payload = "", long long max_attempts = 5, long long delay_ms = 0, const std::string& unique_key = "") const {
        const fg::JobOptions o = options(max_attempts, delay_ms, unique_key);
        return issue_texts(m_q->check(name, payload, o));
    }

    void set_schema(const std::string& name, const job_schema& schema) { guarded([&] { m_q->set_schema(name, schema.native()); }); }
    bool remove_schema(const std::string& name) { return m_q->remove_schema(name); }
    bool has_schema(const std::string& name) const { return m_q->has_schema(name); }

    void limit_attempts(long long n) { if (n < 1 || n > fg::kMaxJobAttempts) fail("the attempt limit must be from 1 to " + std::to_string(fg::kMaxJobAttempts)); change([&](fg::JobQueueLimits& l) { l.max_attempts = static_cast<std::uint16_t>(n); }); }
    void limit_queue(long long n) { if (n < 1) fail("the queue limit must be at least 1"); change([&](fg::JobQueueLimits& l) { l.max_queued = static_cast<std::size_t>(n); }); }
    void limit_dead_letters(long long n) { change([&](fg::JobQueueLimits& l) { l.dead_letter_capacity = count_of_size(n, "the dead-letter limit"); }); }
    void limit_payload(long long bytes) { change([&](fg::JobQueueLimits& l) { l.max_payload_bytes = count_of_size(bytes, "the payload limit"); }); }
    void limit_delay(long long ms) { if (ms < 0) fail("the delay limit can't be negative"); change([&](fg::JobQueueLimits& l) { l.max_delay = std::chrono::milliseconds(ms); }); }
    void require_handlers(bool on) { change([&](fg::JobQueueLimits& l) { l.require_handler = on; }); }
    void require_schemas(bool on) { change([&](fg::JobQueueLimits& l) { l.require_schema = on; }); }
    long long attempt_limit() const { return m_q->limits().max_attempts; }

    void set_retry_delay(long long initial_ms, long long max_ms) {
        m_initial = initial_ms;
        m_max = max_ms;
    }

    void start() { m_q->start(); }
    void stop(bool drain = false) { m_q->stop(drain); }
    bool running() const { return m_q->running(); }
    bool wait_idle(long long timeout_ms) { return m_q->wait_idle(std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms)); }
    bool cancel(const std::string& id) { return m_q->cancel(id); }
    bool retry_dead(const std::string& id) { return m_q->retry_dead(id); }
    long long retry_all_dead() { return static_cast<long long>(m_q->retry_all_dead()); }
    bool discard_dead(const std::string& id) { return m_q->discard_dead(id); }
    long long clear_dead() { return static_cast<long long>(m_q->clear_dead()); }
    long long queued() const { return static_cast<long long>(m_q->stats().queued); }
    long long dead() const { return static_cast<long long>(m_q->stats().dead); }
    long long succeeded() const { return static_cast<long long>(m_q->stats().succeeded); }
    long long rejected() const { return static_cast<long long>(m_q->stats().rejected); }
    long long dead_dropped() const { return static_cast<long long>(m_q->stats().dead_evicted); }

    std::vector<std::string> errors_of(const std::string& id) const {
        const auto r = m_q->find(id);
        return r ? r->errors : std::vector<std::string>();
    }

    long long attempts_of(const std::string& id) const {
        const auto r = m_q->find(id);
        return r ? static_cast<long long>(r->attempts) : 0;
    }
    std::vector<std::string> dead_ids() const {
        std::vector<std::string> out;
        for (const auto& r : m_q->dead_letters()) out.push_back(r.id);
        return out;
    }

    std::string state_of(const std::string& id) const {
        const auto r = m_q->find(id);
        return r ? fg::to_string(r->state) : std::string("unknown");
    }

private:
    std::shared_ptr<fg::JobQueue> m_q;
    long long                     m_initial = 1000;
    long long                     m_max = 300000;

    fg::JobOptions options(long long max_attempts, long long delay_ms, const std::string& unique_key) const {
        if (max_attempts < 1 || max_attempts > fg::kMaxJobAttempts) fail("max_attempts must be from 1 to " + std::to_string(fg::kMaxJobAttempts) + ", not " + std::to_string(max_attempts));
        if (delay_ms < 0) fail("delay_ms can't be negative");
        fg::JobOptions o;
        o.retry.max_attempts = static_cast<std::uint16_t>(max_attempts);
        o.delay = std::chrono::milliseconds(delay_ms);
        o.unique_key = unique_key;
        o.retry.initial = std::chrono::milliseconds(m_initial < 0 ? 0 : m_initial);
        o.retry.maximum = std::chrono::milliseconds(m_max < m_initial ? m_initial : m_max);
        return o;
    }

    template <typename Fn>
    void change(Fn&& fn) {
        fg::JobQueueLimits l = m_q->limits();
        fn(l);
        guarded([&] { m_q->set_limits(l); });
    }
};

class budget_monitor {
public:
    budget_monitor() : m_b(std::make_shared<fg::BudgetMonitor>()) {}

    void set_budget(const std::string& metric, double limit, long long period_seconds = 86400, double warn_ratio = 0.8, bool hard_stop = false, const std::string& unit = "") {
        fg::Budget b;
        b.metric = metric;
        b.limit = limit;
        b.period = std::chrono::seconds(period_seconds < 1 ? 1 : period_seconds);
        b.warn_ratio = warn_ratio;
        b.hard_stop = hard_stop;
        b.unit = unit;
        m_b->set_budget(b);
    }

    std::string add(const std::string& metric, double amount = 1.0) { return fg::to_string(m_b->add(metric, amount)); }
    bool allows(const std::string& metric, double amount = 1.0) const { return m_b->allows(metric, amount); }
    bool consume(const std::string& metric, double amount = 1.0) { return m_b->consume(metric, amount); }
    double used(const std::string& metric) const { const auto u = m_b->usage(metric); return u ? u->used : 0.0; }
    std::string level(const std::string& metric) const { const auto u = m_b->usage(metric); return u ? fg::to_string(u->level) : std::string("ok"); }
    void reset(const std::string& metric) { m_b->reset(metric); }
    void set_gauge_threshold(const std::string& metric, double warn, double critical) { m_b->set_gauge_threshold(metric, warn, critical); }
    std::string set_gauge(const std::string& metric, double value) { return fg::to_string(m_b->set_gauge(metric, value)); }

    template <typename F>
    void on_alert(F callback) {
        m_b->on_alert([callback](const fg::BudgetAlert& a) mutable { callback(a.metric, std::string(fg::to_string(a.level)), a.used, a.limit); });
    }

    const std::shared_ptr<fg::BudgetMonitor>& raw() const noexcept { return m_b; }

private:
    std::shared_ptr<fg::BudgetMonitor> m_b;
};

class debouncer {
public:
    debouncer() = default;

    template <typename F>
    debouncer(F callback, long long wait_ms, bool leading = false, bool trailing = true, long long max_wait_ms = 0) {
        fg::DebounceOptions o;
        o.wait = std::chrono::milliseconds(wait_ms < 0 ? 0 : wait_ms);
        o.leading = leading;
        o.trailing = trailing;
        o.max_wait = std::chrono::milliseconds(max_wait_ms < 0 ? 0 : max_wait_ms);
        m_d = std::make_shared<fg::Debouncer>([callback]() mutable { callback(); }, o);
    }

    void call() { need().call(); }
    void cancel() { need().cancel(); }
    void flush() { need().flush(); }
    bool pending() const { return m_d && m_d->pending(); }

private:
    std::shared_ptr<fg::Debouncer> m_d;

    fg::Debouncer& need() const {
        if (!m_d) fail("this debouncer has no callback; make one with Debouncer(callback, wait_ms)");
        return *m_d;
    }
};

class throttler {
public:
    throttler() = default;

    template <typename F>
    throttler(F callback, long long interval_ms, bool trailing = true) {
        m_t = std::make_shared<fg::Throttler>([callback]() mutable { callback(); }, std::chrono::milliseconds(interval_ms < 1 ? 1 : interval_ms), trailing);
    }

    void call() { if (!m_t) fail("this throttler has no callback"); m_t->call(); }
    void cancel() { if (m_t) m_t->cancel(); }
    bool pending() const { return m_t && m_t->pending(); }

private:
    std::shared_ptr<fg::Throttler> m_t;
};

class keyed_lock {
public:
    keyed_lock() : m_k(std::make_shared<fg::KeyedMutex>()) {}

    template <typename F>
    void with(const std::string& key, F body) {
        auto l = m_k->lock(key);
        body();
    }

    bool locked(const std::string& key) const { return m_k->locked(key); }

private:
    std::shared_ptr<fg::KeyedMutex> m_k;
};

class sql_statement {
public:
    sql_statement() = default;
    explicit sql_statement(const std::string& sql) : m_s(std::make_shared<fg::Statement>(guarded([&] { return fg::Statement(sql); }))) {}

    sql_statement& bind_int(long long v) { guarded([&] { need().bind(static_cast<std::int64_t>(v)); }); return *this; }
    sql_statement& bind_float(double v) { guarded([&] { need().bind(v); }); return *this; }
    sql_statement& bind_text(const std::string& v) { guarded([&] { need().bind(v); }); return *this; }
    sql_statement& bind_bool(bool v) { guarded([&] { need().bind(v); }); return *this; }
    sql_statement& bind_null() { guarded([&] { need().bind_null(); }); return *this; }
    sql_statement& append(const std::string& fragment) { guarded([&] { need().append(fragment); }); return *this; }

    std::string sql() const { return m_s ? m_s->sql() : std::string(); }
    long long placeholders() const { return m_s ? static_cast<long long>(m_s->placeholder_count()) : 0; }
    long long bound() const { return m_s ? static_cast<long long>(m_s->params().size()) : 0; }
    bool complete() const { return m_s && m_s->complete(); }
    std::string shape() const { return m_s ? m_s->shape() : std::string(); }
    std::string debug_string() const { return m_s ? m_s->debug_string() : std::string(); }

    static std::string identifier(const std::string& name) { return guarded([&] { return fg::Statement::identifier(name); }); }
    static std::string placeholders_for(long long count) { return fg::Statement::placeholders(static_cast<std::size_t>(count < 0 ? 0 : count)); }

private:
    std::shared_ptr<fg::Statement> m_s;

    fg::Statement& need() const {
        if (!m_s) fail("this statement is empty");
        return *m_s;
    }
};

inline std::string hash_secret(const std::string& secret) { return fg::hash_secret(secret); }

} // namespace guard
} // namespace walnut_rt

#endif // WALNUT_GUARD_HPP
