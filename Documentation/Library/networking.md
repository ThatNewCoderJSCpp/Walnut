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
