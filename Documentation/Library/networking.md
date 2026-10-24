# networking

`networking` has TCP and UDP sockets, DNS lookups, URLs, an HTTP client and server, TLS connections, WebSockets, and an event loop for doing several of these at once. It is built on Fizmo's networking library.

```walnut
import module.networking
```

Everything is in the namespace `std::networking`. Byte data, such as what a socket sends and receives, is a `string`.

## A first example

This program serves HTTP and asks itself a question:

```walnut
import module.io
import module.networking

using std::networking::Request;
using std::networking::Response;

function main() -> int {
    std::networking::HttpServer: server;
    server.handle([](Request: req) -> Response {
        if (req.path() == "/hello") then {
            return Response::plain("hello, " + req.param("name"));
        }
        return Response::not_found();
    });
    server.listen("127.0.0.1", 18090);

    auto: reply = std::networking::get("http://127.0.0.1:18090/hello?name=walnut");
    std::io::println(reply.status(), " ", reply.body());
    std::io::println(std::networking::get("http://127.0.0.1:18090/elsewhere").status());
    server.close();
    return 0;
} end function main
```

This prints `200 hello, walnut`, then `404`. The client and the server share one [event loop](#the-event-loop), and a request runs the loop until its answer arrives, so one program can do both.

## HTTP requests

| Function | Does |
|---|---|
| `get(url, timeout_ms = 30000)` | A GET request |
| `head(url, timeout_ms)` | A HEAD request |
| `post(url, body, content_type = "application/x-www-form-urlencoded", timeout_ms)` | A POST |
| `post_json(url, json, timeout_ms)` | A POST with `Content-Type: application/json` |
| `put(url, body, content_type, timeout_ms)`, `patch(url, body, content_type, timeout_ms)` | PUT and PATCH |
| `remove(url, timeout_ms)` | A DELETE request |
| `request(method, url, body = "", timeout_ms)` | Any method |
| `request(url, options)` | A request described by a `RequestOptions` |
| `download(url)` | The body of a successful GET; throws a `std::errors::network_error` otherwise |

`RequestOptions` has the fields `method`, `body`, `headers`, `timeout_ms`, `follow_redirects` (`true`) and `verify_certificates` (`true`), and `header(name, value)` to add a header.

Each gives a `Response`:

| Method | Gives |
|---|---|
| `status()`, `status_text()`, `ok()` | The status code (0 when the request failed), its text, and whether it is 2xx |
| `body()`, `url()` | The body, and the final URL after any redirects |
| `header(name)`, `has_header(name)`, `header_names()` | Headers, with names matched without regard to case |
| `failed()`, `error()` | Whether the request itself failed (no connection, a timeout, a bad certificate), and why |

A failed request doesn't throw: it gives a `Response` whose `failed()` is `true`.

## HTTP servers

An `HttpServer` calls a handler for each request. The handler takes a `Request` and gives back a `Response`.

| Method | Does |
|---|---|
| `handle(handler)` | Sets the handler |
| `listen(host, port)` | Starts listening; throws when the port can't be used |
| `serve()` | Runs the event loop until it is stopped |
| `serve_for(ms)` | Runs it for a while |
| `close()`, `is_listening()` | |

`serve(host, port, handler)` does all of it in one call, and doesn't return.

A `Request` has `method()`, `target()` (the path and query, as sent), `path()`, `query()`, `param(name)` (from the query), `body()`, `version()`, `header(name)`, `has_header(name)` and `header_names()`.

A `Response` for a server is made with `Response::plain(body, status = 200)`, `Response::html(body, status)`, `Response::json(body, status)`, `Response::redirect(location, status = 302)`, `Response::not_found()` or `Response::make(status, body, content_type)`, and changed with `set_status`, `set_body`, `set_header` and `add_header`. A handler that throws sends a 500 response.

A `Request` on a server that uses a `Guard` also has `request_id()`, `client_ip()`, `tls_fingerprint()` (the JA4 hash on HTTPS), `risk()` (`"trusted"`, `"low"`, `"medium"`, `"high"` or `"blocked"`), `risk_score()`, `encrypted()`, `user()` and `guarded()`.

## Server safety

`std::networking::guard::Guard` adds safety checks to an `HttpServer`. Pass it to `use_guard` before `listen`:

```walnut
using std::networking::guard::Guard;

Guard: shield;
shield.rate_limit(5.0, 30.0);
shield.route_limit("POST", "/login", 5.0, 5.0);

std::networking::HttpServer: server;
server.use_guard(shield);
server.handle([shield](Request: req) -> Response {
    if (req.path() == "/login") then {
        const auto: check = shield.check_login(req, "ann");
        if (!check.allowed()) then {
            return Response::json("{\"error\":\"locked\"}", check.status());
        }
        return Response::json("{\"csrf\":\"" + shield.login_user(req, "ann") + "\"}");
    }
    if (!shield.authenticated(req)) then {
        return Response::json("{\"error\":\"login\"}", 401);
    }
    return Response::plain("hello " + shield.user(req));
});
server.listen("0.0.0.0", 8080);
shield.start_maintenance();
server.serve();
```

For every request, the guard:

- gives it a request ID (sent back as `X-Request-Id`)
- finds the client's IP, trusting `X-Forwarded-For` only from proxies named with `trusted_proxy`
- adds security headers (`nosniff`, frame denial, a referrer policy, CSP, HSTS on HTTPS, `Cache-Control: no-store`)
- checks the TLS fingerprint against the `User-Agent`, to spot scripts that claim to be browsers
- rate limits each IP, and each route given to `route_limit`, answering 429 with `Retry-After`
- locks out IPs that fail too often, for longer each time
- turns a thrown error into a 500 that shows only the request ID, and groups the error for `errors_json()`
- writes one access-log line when the response is sent, with passwords, tokens and cookies hidden

| Setting | Does |
|---|---|
| `rate_limit(per_second, burst)` | tokens come back at `per_second`; a client starts with, and can save up, `burst` |
| `no_rate_limit()` | turns the per-IP limit off |
| `route_limit(method, path_prefix, per_minute, burst)` | a stricter limit for one route, such as `/login` |
| `trust_proxy(on)`, `trusted_proxy(ip)` | |
| `block_bots(on)` | refuses requests whose fingerprint looks like a script |
| `block_fingerprint(ja4, label)`, `trust_fingerprint(ja4, label)` | |
| `track_failures(on)`, `access_log(on)`, `security_headers(on)`, `no_store(on)` | each is on by default |
| `content_security_policy(policy)`, `hsts(value)`, `allowed_origin(origin)`, `max_body(bytes)` | |
| `session_timeouts(idle_minutes, absolute_hours)`, `cookie_same_site(mode)`, `secure_cookies(on)` | |
| `use_logger(log)` | sends the guard's logs to a `Logger` |

### Logging in and out

| Method | Does |
|---|---|
| `check_login(req, account)` | before checking a password: a `LoginCheck` with `allowed()`, `status()`, `retry_after_ms()`, `delay_ms()` and `reason()` |
| `login_failed(req, account)` | records a wrong password |
| `login(req, user, roles)`, `login_user(req, user)` | ends any old session, starts a new one, sets the session and device cookies, and returns the CSRF token |
| `authenticated(req)`, `user(req)`, `has_role(req, role)`, `session_id(req)` | |
| `csrf_token(req)`, `csrf_ok(req)`, `same_origin(req)` | `csrf_ok` checks the `X-CSRF-Token` header |
| `new_device(req)`, `trust_device(req, label)` | whether this login came from a device the user hasn't used before |
| `logout(req, everywhere, keep_device)` | ends the session on the server, expires its cookie, and sends `Clear-Site-Data` so the browser clears cookies, local storage and session storage |
| `revoke_sessions(user)`, `session_count(user)` | |

### Other protections

`register_owner(type, lookup)` tells the guard who owns each object of a type: `lookup` takes an ID and returns its owner's user ID, or `""` when there isn't one. `can(req, type, id, action)` is then true only for the owner (or a role given to `admin_role`), so a user can't reach someone else's data by changing an ID in the URL. Answer 404 when it's false, so the IDs of other people's objects stay hidden.

`idempotent(req, body)` runs `body` once for each `Idempotency-Key` header: a retry with the same key gets the first response back, the same key with a different body gets 422, and a retry while the first is still running gets 409. Requests without the header just run.

`set_budget(metric, limit, period_seconds, warn_ratio, hard_stop)`, `budget_level(metric)` and `on_budget_alert(callback)` raise spend alerts; `on_new_error(callback)` is called the first time each kind of error is seen. `start_maintenance()` clears out old sessions and limits in the background.

### The pieces on their own

The rest of `std::networking::guard` works without a server:

| Class | Does |
|---|---|
| `RateLimiter(per_second, burst, start_tokens)`, `RateLimiter::per_minute(n, burst)` | token buckets per key; `start_tokens` defaults to `burst`: `consume(key, cost)` gives a `RateDecision`, `allow(key)` a `bool`; also `refund`, `reset` |
| `WindowLimiter(max_hits, window_seconds)` | at most `max_hits` per window: `hit(key)` |
| `FailureTracker(max_failures, window_seconds, lockout_seconds)` | `record_failure(key)` and `record_success(key)` give a `FailureStatus` with `failure_ratio()`, `locked()` and `lock_remaining_ms()` |
| `Logger` | `to_stderr()`, `to_file(path, max_bytes, files)`, `as_text()`, `set_level`, `redact(key)`, `info_with(message, fields)` with `LogFields` |
| `JobQueue(workers)` | background jobs that are checked before they're queued; see [Background jobs](#background-jobs) |
| `BudgetMonitor` | `set_budget`, `add(metric, amount)` (returns the level), `consume` (refuses past a hard stop), gauges and `on_alert` |
| `Debouncer(callback, wait_ms, leading, trailing, max_wait_ms)`, `Throttler(callback, interval_ms)` | `call()` as often as you like; `callback` runs once things go quiet, or at most once per interval |
| `KeyedLock` | `with(key, body)` runs `body` while no other thread holds the same key, so two requests can't change one record at once |
| `SqlStatement(sql)` | a parameterized query: `?` placeholders, `bind_int`, `bind_text` and the others; it refuses SQL with values pasted into it or with more than one statement |
| `hash_secret(secret)` | a SHA-256 hash, for storing API keys and tokens |

### Background jobs

A `JobQueue` runs jobs on worker threads, and checks each job before it goes into the queue:

```walnut
using std::networking::guard::JobQueue;
using std::networking::guard::JobSchema;
using std::networking::guard::FieldRule;
using std::networking::guard::Job;

JobSchema: file;
file.add("name", FieldRule::text_field().non_empty());
file.add("bytes", FieldRule::int_field().between(1, 1000000));

JobSchema: email;
email.add("to", FieldRule::text_field().format("email"));
email.add("subject", FieldRule::text_field().length(1, 200));
email.add("files", FieldRule::list_of(std::networking::guard::object_of(file)).max_items(3).optional());

JobQueue: jobs(2);
jobs.on_checked("email", email, [](Job: j) -> bool {
    return send(j.payload());
});
jobs.start();

auto: r = jobs.try_enqueue("email", "{\"to\":\"ann@\",\"subject\":\"Hi\"}", 3);
if (!r.ok()) then {
    std::io::println(r.message());
}
```

This prints `$.to: must be a valid email address`. `enqueue` makes the same checks and throws on a bad job. `try_enqueue` returns an `EnqueueResult` with `ok()`, `id()`, `duplicate()`, `problems()` and `message()`. `check(...)` reports the problems without queueing anything.

A job is refused when:

- no handler is registered for its name
- `max_attempts` isn't from 1 to 100, or is above `limit_attempts(n)`, so no job retries forever
- its delay is too long or its unique key is too long
- its payload is over 64 KB (`limit_payload`) or doesn't match its schema
- the queue is full (`limit_queue`)

`require_schemas(true)` also refuses jobs that have no schema.

| Rule | Checks |
|---|---|
| `FieldRule::text_field()`, `int_field()`, `float_field()`, `bool_field()`, `any_field()` | the type |
| `FieldRule::list_of(rule)`, `object_of(schema)` | each item, and nested fields |
| `min`, `max`, `between`, `positive` | number ranges |
| `min_length`, `max_length`, `length`, `non_empty` | text length in characters |
| `min_items`, `max_items`, `unique_items` | list sizes |
| `one_of(values)` | allowed values |
| `format(name)` | `"email"`, `"uuid"`, `"url"`, `"date"`, `"datetime"`, `"identifier"`, `"hex"`, `"base64"` or `"digits"` |
| `optional()`, `nullable()`, `allow_control_chars()` | relaxations |

Fields are required unless they're `optional()`. Fields the schema doesn't list are refused unless it has `allow_extra_fields()`. A schema also has `at_least_one_of`, `only_one_of` and `all_or_none_of` for groups of fields, and limits on `max_bytes`, `max_depth`, `max_string_length` and `max_array_items`. `JobSchema::text_payload(max_bytes)` is for plain-text payloads and `JobSchema::no_payload()` for jobs that take none. `validate(payload)` lists the problems on its own.

A handler that returns `false` or throws is retried with growing delays (`set_retry_delay(initial_ms, max_ms)`). After its last attempt, `Job` has `attempt()`, `max_attempts()` and `last_attempt()`, and the job moves to the dead-letter queue. That queue is separate, so the workers carry on with other jobs. `dead()`, `dead_ids()`, `errors_of(id)` (one per attempt) and `attempts_of(id)` show what failed. `retry_dead(id)` and `retry_all_dead()` check the payload again and requeue the job. `discard_dead(id)` and `clear_dead()` remove dead jobs. The dead-letter queue keeps 10,000 jobs (`limit_dead_letters`); past that, the oldest are dropped and counted by `dead_dropped()`. `wait_idle(ms)` waits until nothing is running or waiting to retry.

## TCP

```walnut
import module.io
import module.networking
import module.threading

function main() -> int {
    auto: listener = std::networking::TcpListener::bind("127.0.0.1", 18091);
    std::threading::Thread: client = std::threading::Thread([]() -> void {
        auto: s = std::networking::TcpStream::connect("127.0.0.1", 18091);
        s.write_line("ping");
        std::io::println("client got: ", s.read_line());
        s.close();
    });
    auto: conn = listener.accept();
    conn.write_line(conn.read_line() + " pong");
    client.join();
    conn.close();
    listener.close();
    return 0;
} end function main
```

This prints `client got: ping pong`.

A `TcpStream` is made with `TcpStream::connect(host, port, timeout_ms = 10000)`, which looks up host names and throws when it can't connect. It has:

| Method | Does |
|---|---|
| `send(data)`, `send_all(data)` | Sends some or all of `data`; `send` gives how many bytes went |
| `write_line(line)` | Sends `line` and a newline |
| `receive(max_bytes = 65536)` | What has arrived, up to `max_bytes`; an empty string means the other side closed |
| `receive_exact(count)` | Exactly `count` bytes, unless the connection ends first |
| `read_line()` | One line, without its `\n` or `\r\n` |
| `receive_all()` | Everything until the other side closes |
| `set_timeout(ms)` | A time limit for each send and receive |
| `is_open()`, `close()`, `local_address()`, `remote_address()`, `last_error()` | |

A `TcpListener` is made with `TcpListener::bind(host, port, backlog = 16)`; port 0 picks a free port, which `port()` then tells. `accept()` waits for a connection and gives its `TcpStream`.

## UDP

`UdpSocket::bind(host, port)` gives a socket that receives on that address; a plain `UdpSocket` can only send. `send_to(host, port, data)` sends a datagram, `receive_from(max_bytes = 65507)` waits for one and gives a `Datagram` with `data()`, `host()`, `port()` and `empty()`, and `broadcast(port, data)` sends to the local network. `set_timeout(ms)`, `port()`, `local_address()` and `close()` work as for TCP.

## TLS

`TlsStream::connect(host, port, verify = true, timeout_ms = 10000)` opens an encrypted connection. With `verify`, the server's certificate has to be valid for `host`. It has `send_all(data)`, `receive(max_bytes)`, `receive_all()`, `is_open()` and `close()`. HTTPS and `wss://` URLs use TLS on their own.

## WebSockets

```walnut
import module.io
import module.networking

function main() -> int {
    std::networking::WebSocketServer: echo;
    echo.handle([](std::networking::WebSocket: ws) -> void {
        ws.on_message([ws](std::networking::Message: m) -> void {
            auto: reply = ws;
            reply.send_text("echo: " + m.data());
        });
    });
    echo.listen("127.0.0.1", 18092);

    auto: ws = std::networking::WebSocket::connect("ws://127.0.0.1:18092/");
    ws.send_text("hi there");
    std::io::println(ws.receive(3000).data());
    ws.close();
    echo.close();
    return 0;
} end function main
```

This prints `echo: hi there`.

A `WebSocket` is made with `WebSocket::connect(url, timeout_ms = 30000)` (a `ws://` or `wss://` URL), or handed to a server's handler. It has:

| Method | Does |
|---|---|
| `send_text(text)`, `send_binary(data)` | Sends a message |
| `receive(timeout_ms = -1)` | Waits for the next message (-1 waits as long as it takes). Gives a `Message` with `data()`, `is_text()`, `is_binary()` and `ok()`, which is `false` when nothing came |
| `on_message(callback)` | Calls `callback` with each `Message` instead of keeping it for `receive` |
| `close(code = 1000, reason = "")` | Closes it and waits for the other side |
| `is_open()`, `close_code()`, `close_reason()`, `error()`, `remote_address()` | |

A `WebSocketServer` has `handle(callback)`, which is called with each new connection, `listen(host, port)`, `broadcast(text)` (to every client), `client_count()`, `port()`, `serve()`, `serve_for(ms)` and `close()`.

## The event loop

HTTP, WebSockets and timers share one event loop. The blocking calls above run it while they wait, so a simple program never has to think about it. To do several things at once, start them and then run the loop:

| Function | Does |
|---|---|
| `get_async(url, callback)`, `post_async(url, body, content_type, callback)`, `request_async(url, options, callback)` | Starts a request; `callback` is called with the `Response` |
| `set_timeout(ms, callback)`, `set_interval(ms, callback)` | Calls `callback` once after `ms`, or every `ms`. Each gives a `Timer` with `cancel()` and `active()` |
| `next_tick(callback)` | Calls `callback` the next time the loop runs |
| `run()` | Runs the loop until there is nothing left to do, or `stop()` is called |
| `run_once()`, `poll()`, `run_for(ms)` | Runs one step (waiting if needed), runs what is ready without waiting, or runs for a while |
| `stop()` | Makes `run()` return |

```walnut
import module.io
import module.networking

function main() -> int {
    long int: ticks = 0;
    auto: every = std::networking::set_interval(10, [&ticks]() -> void { ticks++; });
    std::networking::set_timeout(55, [&every]() -> void { every.cancel(); });
    std::networking::run_for(100);
    std::io::println(ticks >= 4, " ", ticks <= 6);
    return 0;
} end function main
```

This prints `1 1`.

## URLs and names

`Url::parse(text)` reads a URL (throwing when it isn't one), `Url::parse_relative(text, base)` reads one relative to another, and `Url::can_parse(text)` checks without throwing. A `Url` has `href()`, `to_string()`, `origin()`, `scheme()`, `username()`, `password()`, `host()` (with the port), `hostname()`, `port()` (the default port when none is given), `path()`, `query()` (with its `?`), `fragment()` (with its `#`), `path_and_query()`, `secure()`, `param(name)`, `has_param(name)`, `params(name)` and `param_names()`. The `with_` methods give a changed copy: `with_scheme`, `with_host`, `with_port`, `with_path`, `with_query`, `with_fragment`, `with_param(name, value)` and `without_param(name)`. `Url::default_port(scheme)` gives 80 for `http`, 443 for `https` and so on.

`resolve(host)` gives every address of a host name, `resolve4` and `resolve6` just the IPv4 or IPv6 ones, and `reverse_lookup(ip)` the names of an address. `is_ipv4(text)` and `is_ipv6(text)` check an address's form. `url_encode(text)`, `url_decode(text)` and `status_text(code)` are small helpers.
