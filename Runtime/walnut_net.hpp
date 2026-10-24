#ifndef WALNUT_NET_HPP
#define WALNUT_NET_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../networking.hpp"
#include "walnut_guard.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace net {

namespace fn   = fizmo::networking;
namespace core = fizmo::networking::core;
namespace sec  = fizmo::networking::security;

template <typename... T>
struct tag {};

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

template <typename T, typename = void>
struct has_native_member : std::false_type {};

template <typename T>
struct has_native_member<T, std::void_t<decltype(std::declval<T&>().native)>> : std::true_type {};

template <typename T, typename V>
inline T to_walnut(V&& v) {
    if constexpr (std::is_same_v<std::decay_t<V>, T>) return T(std::forward<V>(v));
    else return T(walnut_rt::native_tag{}, std::forward<V>(v));
}

template <typename T, typename V>
inline T from_walnut(V&& v) {
    if constexpr (has_native_member<std::decay_t<V>>::value) return T(v.native);
    else return T(std::forward<V>(v));
}

struct api {};

inline std::uint16_t port_of(long long port) {
    if (port < 0 || port > 65535) fail("port " + std::to_string(port) + " is out of range (0 to 65535)");
    return static_cast<std::uint16_t>(port);
}

inline std::string error_text(const std::error_code& ec) { return ec ? ec.message() : std::string(); }

inline long long ms_now() {
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

template <typename Pred>
inline bool pump_until(Pred done, long long timeout_ms) {
    const long long deadline = timeout_ms < 0 ? -1 : ms_now() + timeout_ms;
    while (!done()) {
        if (deadline >= 0 && ms_now() >= deadline) return false;
        fn::run_for(std::chrono::milliseconds(5));
    }
    return true;
}

inline bool is_ipv4(const std::string& text) {
    int parts = 0;
    std::size_t i = 0;
    while (i <= text.size()) {
        std::size_t j = i;
        int value = 0;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9' && j - i < 3) {
            value = value * 10 + (text[j] - '0');
            ++j;
        }
        if (j == i || value > 255) return false;
        ++parts;
        if (j == text.size()) break;
        if (text[j] != '.') return false;
        i = j + 1;
    }
    return parts == 4;
}

inline bool is_ipv6(const std::string& text) {
    if (text.find(':') == std::string::npos) return false;
    for (char c : text) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex && c != ':' && c != '.') return false;
    }
    return true;
}

inline std::string first_address(const std::string& host) {
    if (host == "localhost") return "127.0.0.1";
    if (host.empty() || host == "0.0.0.0" || is_ipv4(host) || is_ipv6(host)) return host.empty() ? std::string("0.0.0.0") : host;
    core::DNSResolver resolver;
    const core::DNSResult r = resolver.resolve(host);
    if (!r.is_success()) return std::string();
    const std::string v4 = r.first_ipv4();
    return v4.empty() ? r.first_address() : v4;
}

inline std::vector<std::string> resolve(const std::string& host) {
    core::DNSResolver resolver;
    const core::DNSResult r = resolver.resolve(host);
    std::vector<std::string> out;
    for (const auto& a : r.addresses()) out.push_back(a.address);
    return out;
}

inline std::vector<std::string> resolve4(const std::string& host) {
    core::DNSResolver resolver;
    return resolver.resolve(host).get_ipv4_addresses();
}

inline std::vector<std::string> resolve6(const std::string& host) {
    core::DNSResolver resolver;
    return resolver.resolve(host).get_ipv6_addresses();
}

inline std::vector<std::string> reverse_lookup(const std::string& ip) {
    core::DNSResolver resolver;
    const core::DNSResult r = resolver.reverse_lookup(ip);
    std::vector<std::string> out;
    for (const auto& a : r.addresses()) out.push_back(a.address);
    if (out.empty() && !r.hostname().empty() && r.hostname() != ip) out.push_back(r.hostname());
    return out;
}


inline std::string address_text(const core::NetworkAddress& a) { return a.host() + ":" + std::to_string(a.port()); }

class tcp_stream {
public:
    tcp_stream() = default;
    explicit tcp_stream(std::shared_ptr<core::TCPSocket> s) : m_sock(std::move(s)) {}

    static tcp_stream connect(const std::string& host, long long port, long long timeout_ms) {
        const std::string ip = first_address(host);
        if (ip.empty()) fail("can't find the address of '" + host + "'");
        auto s = std::make_shared<core::TCPSocket>(is_ipv6(ip) ? core::AddressFamily::IPv6 : core::AddressFamily::IPv4);
        if (timeout_ms > 0) s->set_connect_timeout(static_cast<unsigned>(timeout_ms));
        if (!s->connect(core::NetworkAddress(ip, port_of(port)))) fail("can't connect to " + host + ":" + std::to_string(port) + ": " + s->last_error().to_string());
        return tcp_stream(std::move(s));
    }

    bool valid() const { return m_sock && m_sock->is_valid(); }
    bool is_open() const { return m_sock && m_sock->is_connected(); }

    long long send(const std::string& data) {
        need();
        return m_sock->send(data.data(), data.size());
    }

    bool send_all(const std::string& data) {
        need();
        std::size_t done = 0;
        while (done < data.size()) {
            const int n = m_sock->send(data.data() + done, data.size() - done);
            if (n <= 0) return false;
            done += static_cast<std::size_t>(n);
        }
        return true;
    }

    std::string receive(long long max_bytes) {
        need();
        if (!m_pending.empty()) {
            const std::size_t take = std::min<std::size_t>(m_pending.size(), static_cast<std::size_t>(max_bytes > 0 ? max_bytes : 65536));
            std::string out = m_pending.substr(0, take);
            m_pending.erase(0, take);
            return out;
        }
        std::string buf(static_cast<std::size_t>(max_bytes > 0 ? max_bytes : 65536), '\0');
        const int n = m_sock->receive(&buf[0], buf.size());
        if (n <= 0) return std::string();
        buf.resize(static_cast<std::size_t>(n));
        return buf;
    }

    std::string receive_exact(long long count) {
        std::string out;
        while (static_cast<long long>(out.size()) < count) {
            const std::string piece = receive(count - static_cast<long long>(out.size()));
            if (piece.empty()) break;
            out += piece;
        }
        return out;
    }

    std::string read_line() {
        need();
        std::string out;
        while (true) {
            const std::size_t nl = m_pending.find('\n');
            if (nl != std::string::npos) {
                out = m_pending.substr(0, nl);
                m_pending.erase(0, nl + 1);
                if (!out.empty() && out.back() == '\r') out.pop_back();
                return out;
            }
            std::string buf(4096, '\0');
            const int n = m_sock->receive(&buf[0], buf.size());
            if (n <= 0) {
                out = m_pending;
                m_pending.clear();
                return out;
            }
            m_pending.append(buf.data(), static_cast<std::size_t>(n));
        }
    }

    std::string receive_all() {
        std::string out;
        while (true) {
            const std::string piece = receive(65536);
            if (piece.empty()) break;
            out += piece;
        }
        return out;
    }

    void set_timeout(long long ms) {
        need();
        m_sock->set_receive_timeout(static_cast<unsigned>(ms < 0 ? 0 : ms));
        m_sock->set_send_timeout(static_cast<unsigned>(ms < 0 ? 0 : ms));
    }

    void close() {
        if (m_sock) m_sock->graceful_close(200);
    }

    std::string local_address() const { return m_sock ? address_text(m_sock->get_local_address()) : std::string(); }
    std::string remote_address() const { return m_sock ? address_text(m_sock->get_remote_address()) : std::string(); }
    std::string last_error() const { return m_sock ? m_sock->last_error().to_string() : std::string(); }

private:
    void need() const {
        if (!m_sock) fail("this TCP stream isn't connected");
    }

    std::shared_ptr<core::TCPSocket> m_sock;
    std::string                      m_pending;
};

class tcp_listener {
public:
    tcp_listener() = default;

    static tcp_listener bind(const std::string& host, long long port, long long backlog) {
        tcp_listener out;
        const std::string ip = first_address(host);
        out.m_sock = std::make_shared<core::TCPSocket>(is_ipv6(ip) ? core::AddressFamily::IPv6 : core::AddressFamily::IPv4);
        int yes = 1;
        out.m_sock->set_option(SOL_SOCKET, SO_REUSEADDR, yes);
        if (!out.m_sock->bind(core::NetworkAddress(ip.empty() ? host : ip, port_of(port)))) fail("can't listen on " + host + ":" + std::to_string(port) + ": " + out.m_sock->last_error().to_string());
        if (!out.m_sock->listen(static_cast<int>(backlog > 0 ? backlog : 16))) fail("can't listen on " + host + ":" + std::to_string(port) + ": " + out.m_sock->last_error().to_string());
        return out;
    }

    tcp_stream accept() {
        if (!m_sock) fail("this TCP listener isn't open");
        std::unique_ptr<core::TCPSocket> c = m_sock->accept();
        if (!c) fail("accept() failed: " + m_sock->last_error().to_string());
        return tcp_stream(std::shared_ptr<core::TCPSocket>(std::move(c)));
    }

    long long port() const { return m_sock ? static_cast<long long>(m_sock->get_local_address().port()) : 0; }
    std::string local_address() const { return m_sock ? address_text(m_sock->get_local_address()) : std::string(); }
    bool is_open() const { return m_sock && m_sock->is_valid(); }

    void close() {
        if (m_sock) m_sock->close();
    }

private:
    std::shared_ptr<core::TCPSocket> m_sock;
};

class datagram {
public:
    datagram() = default;
    datagram(std::string d, std::string h, long long p) : m_data(std::move(d)), m_host(std::move(h)), m_port(p) {}

    const std::string& data() const { return m_data; }
    const std::string& host() const { return m_host; }
    long long port() const { return m_port; }
    bool empty() const { return m_data.empty(); }

private:
    std::string m_data;
    std::string m_host;
    long long   m_port = 0;
};

class udp_socket {
public:
    udp_socket() : m_sock(std::make_shared<core::UDPSocket>(core::AddressFamily::IPv4)) {}

    static udp_socket bind(const std::string& host, long long port) {
        udp_socket out;
        const std::string ip = first_address(host);
        if (is_ipv6(ip)) out.m_sock = std::make_shared<core::UDPSocket>(core::AddressFamily::IPv6);
        if (!out.m_sock->bind(core::NetworkAddress(ip.empty() ? host : ip, port_of(port)))) fail("can't bind a UDP socket to " + host + ":" + std::to_string(port) + ": " + out.m_sock->last_error().to_string());
        return out;
    }

    long long send_to(const std::string& host, long long port, const std::string& data) {
        const std::string ip = first_address(host);
        if (ip.empty()) fail("can't find the address of '" + host + "'");
        return m_sock->send(data.data(), data.size(), core::NetworkAddress(ip, port_of(port)));
    }

    datagram receive_from(long long max_bytes) {
        std::string buf(static_cast<std::size_t>(max_bytes > 0 ? max_bytes : 65507), '\0');
        core::NetworkAddress from;
        const int n = m_sock->receive(&buf[0], buf.size(), from);
        if (n <= 0) return datagram();
        buf.resize(static_cast<std::size_t>(n));
        return datagram(std::move(buf), from.host(), from.port());
    }

    long long broadcast(long long port, const std::string& data) { return m_sock->broadcast(data.data(), data.size(), port_of(port)); }

    void set_timeout(long long ms) {
        m_sock->set_receive_timeout(static_cast<unsigned>(ms < 0 ? 0 : ms));
        m_sock->set_send_timeout(static_cast<unsigned>(ms < 0 ? 0 : ms));
    }

    long long port() const { return static_cast<long long>(m_sock->get_local_address().port()); }
    std::string local_address() const { return address_text(m_sock->get_local_address()); }
    void close() { m_sock->close(); }

private:
    std::shared_ptr<core::UDPSocket> m_sock;
};

class tls_stream {
public:
    tls_stream() = default;

    static tls_stream connect(const std::string& host, long long port, bool verify, long long timeout_ms) {
        tls_stream out;
        const std::string ip = first_address(host);
        if (ip.empty()) fail("can't find the address of '" + host + "'");
        out.m_ctx = std::make_shared<sec::TLSContext>(sec::ContextUse::Client);
        out.m_ctx->set_verify_mode(verify ? sec::VerifyMode::Peer : sec::VerifyMode::None);
        if (!out.m_ctx->initialize()) fail("can't set up TLS for " + host);
        out.m_sock = std::make_shared<sec::SSLSocket>(*out.m_ctx, is_ipv6(ip) ? core::AddressFamily::IPv6 : core::AddressFamily::IPv4);
        (void)timeout_ms;
        if (!out.m_sock->connect(core::NetworkAddress(ip, port_of(port)), host)) fail("can't make a TLS connection to " + host + ":" + std::to_string(port));
        return out;
    }

    bool is_open() const { return m_sock && m_sock->is_handshake_complete() && !m_sock->peer_closed(); }

    bool send_all(const std::string& data) {
        need();
        return m_sock->send_all(data.data(), data.size());
    }

    std::string receive(long long max_bytes) {
        need();
        std::string out;
        if (!m_sock->receive_string(out, static_cast<std::size_t>(max_bytes > 0 ? max_bytes : 16384))) return std::string();
        return out;
    }

    std::string receive_all() {
        std::string out;
        while (true) {
            const std::string piece = receive(16384);
            if (piece.empty()) break;
            out += piece;
        }
        return out;
    }

    void close() {
        if (m_sock) m_sock->close();
    }

private:
    void need() const {
        if (!m_sock) fail("this TLS stream isn't connected");
    }

    std::shared_ptr<sec::TLSContext> m_ctx;
    std::shared_ptr<sec::SSLSocket>  m_sock;
};

class url {
public:
    url() = default;
    explicit url(fn::URL u) : m_url(std::move(u)) {}

    static url parse(const std::string& text) {
        fn::URL u;
        std::string error;
        if (!fn::URL::parse(text, u, &error)) fail("'" + text + "' isn't a valid URL" + (error.empty() ? std::string() : ": " + error));
        return url(std::move(u));
    }

    static url parse_relative(const std::string& text, const std::string& base) {
        fn::URL b;
        std::string error;
        if (!fn::URL::parse(base, b, &error)) fail("'" + base + "' isn't a valid URL");
        fn::URL u;
        if (!fn::URL::parse(text, b, u, &error)) fail("'" + text + "' isn't a valid URL relative to '" + base + "'");
        return url(std::move(u));
    }

    static bool can_parse(const std::string& text) { return fn::URL::can_parse(text); }

    std::string href() const { return m_url.href(); }
    std::string origin() const { return m_url.origin(); }
    std::string scheme() const { return m_url.scheme(); }
    std::string username() const { return m_url.username(); }
    std::string password() const { return m_url.password(); }
    std::string host() const { return m_url.host(); }
    std::string hostname() const { return m_url.bare_hostname(); }
    long long port() const { return static_cast<long long>(m_url.port_number()); }
    std::string path() const { return m_url.pathname(); }
    std::string query() const { return m_url.search(); }
    std::string fragment() const { return m_url.hash(); }
    std::string path_and_query() const { return m_url.path_and_query(); }
    bool secure() const { return m_url.secure(); }
    std::string param(const std::string& name) const { return m_url.search_params().get(name); }
    bool has_param(const std::string& name) const { return m_url.search_params().has(name); }
    std::vector<std::string> params(const std::string& name) const { return m_url.search_params().get_all(name); }

    std::vector<std::string> param_names() const {
        std::vector<std::string> out;
        for (const auto& e : m_url.search_params().entries()) {
            bool seen = false;
            for (const auto& n : out) seen = seen || n == e.first;
            if (!seen) out.push_back(e.first);
        }
        return out;
    }

    url with_scheme(const std::string& v) const { url o = *this; o.m_url.set_protocol(v); return o; }
    url with_host(const std::string& v) const { url o = *this; o.m_url.set_hostname(v); return o; }
    url with_port(long long v) const { url o = *this; o.m_url.set_port(port_of(v)); return o; }
    url with_path(const std::string& v) const { url o = *this; o.m_url.set_pathname(v); return o; }
    url with_query(const std::string& v) const { url o = *this; o.m_url.set_search(v); return o; }
    url with_fragment(const std::string& v) const { url o = *this; o.m_url.set_hash(v); return o; }
    url with_param(const std::string& name, const std::string& value) const {
        url o = *this;
        o.m_url.search_params().set(name, value);
        return o;
    }
    url without_param(const std::string& name) const {
        url o = *this;
        o.m_url.search_params().remove(name);
        return o;
    }

    static long long default_port(const std::string& scheme) { return static_cast<long long>(fn::URL::default_port(scheme)); }

private:
    fn::URL m_url;
};

class request {
public:
    request() = default;

    std::string method() const { return m_method; }
    std::string target() const { return m_target; }
    std::string body() const { return m_body; }
    std::string version() const { return m_version; }
    std::string header(const std::string& name) const { return m_headers.get(name); }
    bool has_header(const std::string& name) const { return m_headers.has(name); }
    std::vector<std::string> header_names() const { return m_headers.names(); }

    std::string path() const {
        const std::size_t q = m_target.find('?');
        return q == std::string::npos ? m_target : m_target.substr(0, q);
    }

    std::string query() const {
        const std::size_t q = m_target.find('?');
        return q == std::string::npos ? std::string() : m_target.substr(q + 1);
    }

    std::string param(const std::string& name) const { return fn::URLSearchParams(query()).get(name); }

    std::string request_id() const { auto* c = context(); return c ? c->request_id : std::string(); }
    std::string client_ip() const { auto* c = context(); return c ? c->client_ip : (m_in ? m_in.socket().remote_address() : std::string()); }
    std::string tls_fingerprint() const { auto* c = context(); return c ? c->tls.ja4 : std::string(); }
    std::string risk() const { auto* c = context(); return c ? std::string(fizmo::networking::guard::to_string(c->verdict.level)) : std::string("low"); }
    long long risk_score() const { auto* c = context(); return c ? static_cast<long long>(c->verdict.score) : 0; }
    bool encrypted() const { return m_in && m_in.socket() && m_in.socket().encrypted(); }
    std::string user() const { auto* c = context(); return c ? c->principal.user_id : std::string(); }
    bool guarded() const { return context() != nullptr; }

    fizmo::networking::guard::RequestContext* context() const { return m_in ? fizmo::networking::guard::Guard::context(m_in) : nullptr; }

    std::string m_method = "GET";
    std::string m_target = "/";
    std::string m_body;
    std::string m_version = "1.1";
    fn::http::Headers m_headers;
    mutable fn::http::IncomingMessage m_in;
    fn::http::ServerResponse          m_out;
};

class response {
public:
    response() = default;

    static response make(long long status, const std::string& body, const std::string& content_type) {
        response r;
        r.m_status = status;
        r.m_body   = body;
        r.m_headers.set("Content-Type", content_type);
        return r;
    }

    long long status() const { return m_status; }
    std::string status_text() const { return m_status_text.empty() ? std::string(fn::http::status_text(static_cast<int>(m_status))) : m_status_text; }
    std::string body() const { return m_body; }
    std::string url() const { return m_url; }
    bool ok() const { return m_status >= 200 && m_status < 300; }
    std::string header(const std::string& name) const { return m_headers.get(name); }
    bool has_header(const std::string& name) const { return m_headers.has(name); }
    std::vector<std::string> header_names() const { return m_headers.names(); }
    std::string error() const { return m_error; }
    bool failed() const { return !m_error.empty(); }

    void set_status(long long code) { m_status = code; }
    void set_body(const std::string& text) { m_body = text; }
    void set_header(const std::string& name, const std::string& value) { m_headers.set(name, value); }
    void add_header(const std::string& name, const std::string& value) { m_headers.append(name, value); }

    long long         m_status = 200;
    std::string       m_status_text;
    std::string       m_body;
    std::string       m_url;
    std::string       m_error;
    fn::http::Headers m_headers;
};

class headers {
public:
    headers() = default;

    void set(const std::string& name, const std::string& value) { m_h.set(name, value); }
    void add(const std::string& name, const std::string& value) { m_h.append(name, value); }
    std::string get(const std::string& name) const { return m_h.get(name); }
    bool has(const std::string& name) const { return m_h.has(name); }
    bool remove(const std::string& name) { return m_h.remove(name); }
    long long size() const { return static_cast<long long>(m_h.size()); }
    std::vector<std::string> names() const { return m_h.names(); }

    fn::http::Headers m_h;
};

inline fn::http::FetchOptions fetch_options(const std::string& method, const std::string& body, const headers& h, long long timeout_ms, bool follow, bool verify) {
    fn::http::FetchOptions o;
    o.method           = method;
    o.body             = body;
    o.headers          = h.m_h;
    o.timeout          = std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 30000);
    o.follow_redirects = follow;
    o.secure.reject_unauthorized = verify;
    return o;
}

inline response from_fetch(const std::error_code& ec, const fn::http::FetchResponse& r) {
    response out;
    if (ec) {
        out.m_status = 0;
        out.m_error  = error_text(ec);
        return out;
    }
    out.m_status      = r.status;
    out.m_status_text = r.status_text;
    out.m_body        = r.body;
    out.m_url         = r.url;
    out.m_headers     = r.headers;
    return out;
}

inline response fetch(const std::string& method, const std::string& target, const std::string& body, const headers& h, long long timeout_ms, bool follow, bool verify) {
    auto result = std::make_shared<response>();
    auto done   = std::make_shared<bool>(false);
    fn::http::fetch(target, fetch_options(method, body, h, timeout_ms, follow, verify), [result, done](const std::error_code& ec, fn::http::FetchResponse r) {
        *result = from_fetch(ec, r);
        *done   = true;
    });
    if (!pump_until([done] { return *done; }, (timeout_ms > 0 ? timeout_ms : 30000) + 1000)) {
        response out;
        out.m_status = 0;
        out.m_error  = "the request timed out";
        return out;
    }
    return *result;
}

template <typename F>
inline void fetch_async(const std::string& method, const std::string& target, const std::string& body, const headers& h, long long timeout_ms, bool follow, bool verify, F callback) {
    fn::http::fetch(target, fetch_options(method, body, h, timeout_ms, follow, verify), [callback](const std::error_code& ec, fn::http::FetchResponse r) mutable {
        callback(to_walnut<arg_of<F, 0>>(from_fetch(ec, r)));
    });
}

namespace fgd = fizmo::networking::guard;

class login_check {
public:
    login_check() = default;
    explicit login_check(const fgd::LoginCheck& c) : m_c(c) {}

    bool allowed() const { return m_c.allowed; }
    long long status() const { return m_c.status; }
    long long retry_after_ms() const { return static_cast<long long>(m_c.retry_after.count()); }
    long long delay_ms() const { return static_cast<long long>(m_c.delay.count()); }
    std::string reason() const { return m_c.reason; }

private:
    fgd::LoginCheck m_c;
};

class guard {
public:
    guard() : m_s(std::make_shared<shared>()) {}

    void rate_limit(double per_second, double burst) { edit().global_limit = fgd::RateLimitPolicy::per_second(per_second, burst); edit().rate_limit = true; }
    void no_rate_limit() { edit().rate_limit = false; }

    void route_limit(const std::string& method, const std::string& path_prefix, double per_minute, double burst) {
        fgd::RouteLimit r;
        r.method = method;
        r.path_prefix = path_prefix;
        r.policy = fgd::RateLimitPolicy::per_minute(per_minute, burst);
        edit().route_limits.push_back(r);
    }

    void trust_proxy(bool on) { edit().trust_proxy = on; }
    void trusted_proxy(const std::string& ip) { edit().trust_proxy = true; edit().trusted_proxies.insert(ip); }
    void block_bots(bool on) { edit().block_high_risk = on; }
    void track_failures(bool on) { edit().track_failures = on; }
    void access_log(bool on) { edit().access_log = on; }
    void security_headers(bool on) { edit().headers.enabled = on; }
    void content_security_policy(const std::string& policy) { edit().headers.content_security_policy = policy; }
    void hsts(const std::string& value) { edit().headers.hsts = value; }
    void no_store(bool on) { edit().no_store_by_default = on; }
    void allowed_origin(const std::string& origin) { edit().allowed_origins.insert(origin); }
    void max_body(long long bytes) { edit().max_body = static_cast<std::size_t>(bytes < 0 ? 0 : bytes); }

    void session_timeouts(long long idle_minutes, long long absolute_hours) {
        edit().session_policy.idle_timeout = std::chrono::minutes(idle_minutes < 1 ? 1 : idle_minutes);
        edit().session_policy.absolute_timeout = std::chrono::hours(absolute_hours < 1 ? 1 : absolute_hours);
    }

    void cookie_same_site(const std::string& mode) {
        auto& c = edit().session_policy.cookie;
        if (mode == "strict") c.same_site = fgd::SameSite::Strict;
        else if (mode == "lax") c.same_site = fgd::SameSite::Lax;
        else if (mode == "none") c.same_site = fgd::SameSite::None;
        else fail("same-site mode must be \"strict\", \"lax\" or \"none\"");
    }

    void secure_cookies(bool on) {
        auto& o = edit();
        o.session_policy.cookie.secure = on;
        o.session_policy.cookie_name = on ? "__Host-session" : "session";
        o.device_policy.cookie_name = on ? "__Host-device" : "device";
    }

    void use_logger(const walnut_rt::guard::logger& log) { if (m_s->core) fail("set the guard's logger before the server starts"); m_s->logger = log.raw(); }

    void block_fingerprint(const std::string& ja4, const std::string& label = "") { core().fingerprints().block(ja4, label); }
    void trust_fingerprint(const std::string& ja4, const std::string& label = "") { core().fingerprints().trust(ja4, label); }

    bool authenticated(const request& req) const { return core().authenticate(in(req)).has_value(); }

    std::string user(const request& req) const {
        const auto s = core().authenticate(in(req));
        return s ? s->user : std::string();
    }

    bool has_role(const request& req, const std::string& role) const {
        core().authenticate(in(req));
        const auto* c = req.context();
        return c && c->principal.has_role(role);
    }

    std::string session_id(const request& req) const {
        const auto s = core().authenticate(in(req));
        return s ? s->id : std::string();
    }

    std::string csrf_token(const request& req) const {
        const auto s = core().authenticate(in(req));
        return s ? s->csrf_token : std::string();
    }

    bool csrf_ok(const request& req) const {
        const std::string method = req.method();
        if (method == "GET" || method == "HEAD" || method == "OPTIONS") return true;
        auto& g = core();
        const std::string token = g.sessions().token_from_cookie_header(req.header("cookie"));
        if (token.empty()) return true;
        return g.sessions().verify_csrf(token, req.header(g.options().csrf_header));
    }

    bool same_origin(const request& req) const {
        const std::string method = req.method();
        if (method == "GET" || method == "HEAD" || method == "OPTIONS") return true;
        const std::string site = req.header("sec-fetch-site");
        if (site == "cross-site") return false;
        std::string origin = req.header("origin");
        if (origin.empty() || origin == "null") origin = req.header("referer");
        if (origin.empty()) return site.empty() || site == "same-origin" || site == "none";
        const std::string host = host_of(origin);
        if (host == lower(req.header("host"))) return true;
        for (const auto& a : core().options().allowed_origins) if (host_of(a) == host) return true;
        return false;
    }

    login_check check_login(const request& req, const std::string& account) const { return login_check(core().check_login(in(req), account)); }
    void login_failed(const request& req, const std::string& account) const { core().login_failed(in(req), account); }

    template <typename V>
    std::string login(const request& req, const std::string& user_id, const V& roles) const {
        std::set<std::string> r;
        for (const auto& x : roles) r.insert(std::string(x));
        const auto result = core().login(in(req), out(req), user_id, r);
        return result.issued.session.csrf_token;
    }

    std::string login_user(const request& req, const std::string& user_id) const {
        const auto result = core().login(in(req), out(req), user_id, {});
        return result.issued.session.csrf_token;
    }

    bool new_device(const request& req) const {
        const auto* c = req.context();
        if (!c || c->device_id.empty()) return false;
        const auto d = core().devices().find(c->principal.user_id, c->device_id);
        return d && !d->trusted;
    }

    bool trust_device(const request& req, const std::string& label = "") const {
        const auto* c = req.context();
        if (!c || c->device_id.empty()) return false;
        return core().devices().trust(c->principal.user_id, c->device_id, label);
    }

    void logout(const request& req, bool everywhere = false, bool keep_device = false) const {
        const auto clear = keep_device ? fgd::ClearSiteData::Storage : (fgd::ClearSiteData::Cookies | fgd::ClearSiteData::Storage);
        core().logout(in(req), out(req), everywhere, clear);
    }

    long long revoke_sessions(const std::string& user_id) const { return static_cast<long long>(core().sessions().revoke_user(user_id)); }
    long long session_count(const std::string& user_id) const { return static_cast<long long>(core().sessions().sessions(user_id).size()); }

    template <typename F>
    void register_owner(const std::string& type, F lookup) {
        core().ownership().register_type(type, [lookup](const std::string& id) mutable -> std::optional<fgd::ResourceInfo> {
            const std::string owner = std::string(lookup(id));
            if (owner.empty()) return std::nullopt;
            fgd::ResourceInfo info;
            info.owner = owner;
            return info;
        });
    }

    void admin_role(const std::string& role) { core().ownership().set_admin_role(role); }

    bool can(const request& req, const std::string& type, const std::string& id, const std::string& action = "read") const {
        auto& g = core();
        g.authenticate(in(req));
        const auto* c = req.context();
        fgd::Principal who = c ? c->principal : fgd::Principal();
        return g.ownership().check(who, type, id, action_of(action)).allowed();
    }

    template <typename F>
    response idempotent(const request& req, F body) const {
        const std::string key = req.header("idempotency-key");
        if (key.empty()) return from_walnut<response>(body());
        auto& g = core();
        g.authenticate(in(req));
        const auto* c = req.context();
        const std::string scope = c && !c->principal.user_id.empty() ? "user:" + c->principal.user_id : "ip:" + req.client_ip();
        const std::string fp = fgd::IdempotencyStore::request_fingerprint(req.method(), req.path(), req.body());
        const fgd::IdempotencyBegin b = g.idempotency().begin(scope, key, fp);

        if (b.state == fgd::IdempotencyState::Replay) {
            response r = response::make(b.response->status, b.response->body, "text/plain; charset=utf-8");
            r.m_headers.clear();
            for (const auto& h : b.response->headers) r.m_headers.append(h.first, h.second);
            r.m_headers.set("Idempotent-Replayed", "true");
            return r;
        }

        if (b.state == fgd::IdempotencyState::InProgress) return response::make(409, "{\"error\":\"request_in_progress\"}", "application/json");
        if (b.state == fgd::IdempotencyState::Mismatch) return response::make(422, "{\"error\":\"idempotency_key_reused\"}", "application/json");
        if (b.state == fgd::IdempotencyState::Invalid) return response::make(400, "{\"error\":\"invalid_idempotency_key\"}", "application/json");
        response r;

        try {
            r = from_walnut<response>(body());
        } catch (...) {
            g.idempotency().abandon(scope, key);
            throw;
        }

        if (r.m_status >= 500) {
            g.idempotency().abandon(scope, key);
        } else {
            fgd::StoredResponse stored;
            stored.status = static_cast<int>(r.m_status);
            stored.body = r.m_body;
            for (const auto& h : r.m_headers.entries()) if (!fn::http::Headers::iequals(h.first, "set-cookie")) stored.headers.push_back(h);
            g.idempotency().complete(scope, key, std::move(stored));
        }

        return r;
    }

    void set_budget(const std::string& metric, double limit, long long period_seconds = 86400, double warn_ratio = 0.8, bool hard_stop = false) {
        fgd::Budget b;
        b.metric = metric;
        b.limit = limit;
        b.period = std::chrono::seconds(period_seconds < 1 ? 1 : period_seconds);
        b.warn_ratio = warn_ratio;
        b.hard_stop = hard_stop;
        core().budgets().set_budget(b);
    }

    std::string budget_level(const std::string& metric) const {
        const auto u = core().budgets().usage(metric);
        return u ? fgd::to_string(u->level) : std::string("ok");
    }

    template <typename F>
    void on_budget_alert(F callback) {
        core().budgets().on_alert([callback](const fgd::BudgetAlert& a) mutable { callback(a.metric, std::string(fgd::to_string(a.level)), a.used, a.limit); });
    }

    template <typename F>
    void on_new_error(F callback) {
        core().errors().on_alert([callback](fgd::ErrorAlertKind kind, const fgd::ErrorGroup& g) mutable {
            callback(std::string(fgd::to_string(kind)), g.type, g.message, static_cast<long long>(g.count));
        });
    }

    long long error_groups() const { return static_cast<long long>(core().errors().groups().size()); }

    std::string errors_json() const {
        std::string out = "[";
        bool first = true;

        for (const auto& g : core().errors().groups()) {
            if (!first) out += ",";
            first = false;
            out += "{\"id\":\"" + fgd::json_escape(g.id) + "\",\"type\":\"" + fgd::json_escape(g.type) + "\",\"message\":\"" + fgd::json_escape(g.message)
                 + "\",\"count\":" + std::to_string(g.count) + ",\"recent\":" + std::to_string(g.recent) + ",\"last_request\":\"" + fgd::json_escape(g.last_request_id) + "\"}";
        }

        return out + "]";
    }

    void start_maintenance() { core().start_maintenance(); }
    void stop_maintenance() { core().stop_maintenance(); }
    void sweep() { core().sweep(); }

    fgd::Guard& core() const {
        if (!m_s->core) m_s->core = std::make_shared<fgd::Guard>(m_s->options, m_s->logger ? m_s->logger : default_logger());
        return *m_s->core;
    }

private:
    struct shared {
        fgd::GuardOptions               options;
        std::shared_ptr<fgd::Logger>    logger;
        std::shared_ptr<fgd::Guard>     core;
    };

    std::shared_ptr<shared> m_s;

    fgd::GuardOptions& edit() {
        if (m_s->core) fail("change the guard's settings before the server starts or the guard is first used");
        return m_s->options;
    }

    static std::shared_ptr<fgd::Logger> default_logger() {
        auto l = std::make_shared<fgd::Logger>(fgd::LogLevel::Info);
        l->add_sink(std::make_shared<fgd::StreamSink>(stderr));
        return l;
    }

    static fn::http::IncomingMessage& in(const request& req) {
        if (!req.m_in || !req.context()) fail("this request didn't come through a guarded server; call server.use_guard(guard) before server.listen()");
        return req.m_in;
    }

    static fn::http::ServerResponse& out(const request& req) {
        in(req);
        return const_cast<fn::http::ServerResponse&>(req.m_out);
    }

    static std::string lower(std::string s) {
        for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    }

    static std::string host_of(const std::string& origin) {
        const std::size_t scheme = origin.find("://");
        std::string rest = scheme == std::string::npos ? origin : origin.substr(scheme + 3);
        const std::size_t slash = rest.find('/');
        if (slash != std::string::npos) rest = rest.substr(0, slash);
        return lower(rest);
    }

    static fgd::Action action_of(const std::string& a) {
        if (a == "read") return fgd::Action::Read;
        if (a == "create") return fgd::Action::Create;
        if (a == "update") return fgd::Action::Update;
        if (a == "delete") return fgd::Action::Delete;
        if (a == "share") return fgd::Action::Share;
        fail("action must be read, create, update, delete or share, not \"" + a + "\"");
    }
};

class http_server {
public:
    http_server() : m_state(std::make_shared<state>()) {}

    template <typename F>
    void handle(F handler) {
        m_state->handler = [handler](const request& r) mutable -> response {
            return from_walnut<response>(handler(to_walnut<arg_of<F, 0>>(r)));
        };
        m_state->has_handler = true;
    }

    void use_guard(const guard& g) {
        if (m_state->listening) fail("call use_guard() before listen()");
        m_state->shield = g;
        m_state->guarded = true;
    }

    void listen(const std::string& host, long long port) {
        if (!m_state->has_handler) fail("give the server a handler before calling listen()");
        auto st = m_state;

        fn::http::RequestHandler h = [st](fn::http::IncomingMessage in, fn::http::ServerResponse out) {
            auto req = std::make_shared<request>();
            req->m_method  = in.method();
            req->m_target  = in.url();
            req->m_version = in.http_version();
            req->m_headers = in.headers();
            req->m_in      = in;
            req->m_out     = out;
            const std::size_t limit = st->guarded ? st->shield.core().options().max_body : (16u << 20);

            in.body([st, req, out](const std::error_code& ec, std::string text) mutable {
                if (out.writable_ended()) return;
                response r;

                if (ec) {
                    r = response::make(413, "request body too large", "text/plain; charset=utf-8");
                } else {
                    req->m_body = std::move(text);

                    try {
                        r = st->handler(*req);
                    } catch (const std::exception& e) {
                        if (st->guarded) {
                            const auto* c = req->context();
                            st->shield.core().errors().capture_exception(e, "walnut " + req->m_method + " " + req->path(), c ? c->request_id : std::string());
                            r = response::make(500, "{\"error\":\"internal_error\",\"request_id\":\"" + (c ? c->request_id : std::string()) + "\"}", "application/json");
                        } else {
                            r = response::make(500, std::string("internal error: ") + e.what(), "text/plain; charset=utf-8");
                        }
                    }
                }

                if (out.writable_ended()) return;
                out.status(static_cast<int>(r.m_status));
                std::vector<std::string> replaced;

                for (const auto& e : r.m_headers.entries()) {
                    if (fn::http::Headers::iequals(e.first, "set-cookie")) continue;
                    bool seen = false;
                    for (const auto& n : replaced) if (fn::http::Headers::iequals(n, e.first)) { seen = true; break; }
                    if (!seen) { out.remove_header(e.first); replaced.push_back(e.first); }
                }

                for (const auto& e : r.m_headers.entries()) out.append_header(e.first, e.second);
                out.end(fn::DataView(r.m_body));
            }, limit);
        };

        st->server = fn::http::create_server(st->guarded ? st->shield.core().wrap(h) : h);
        st->server.on_error([st](const std::error_code& ec) { st->error = error_text(ec); });
        st->server.listen(port_of(port), host, [st] { st->listening = true; });
        pump_until([st] { return st->listening || !st->error.empty(); }, 5000);
        if (!st->error.empty()) fail("the HTTP server can't listen on " + host + ":" + std::to_string(port) + ": " + st->error);
    }

    void serve() { fn::run(); }
    void serve_for(long long ms) { fn::run_for(std::chrono::milliseconds(ms < 0 ? 0 : ms)); }

    void close() {
        if (m_state->server) m_state->server.close();
        fn::run_for(std::chrono::milliseconds(20));
    }

    bool is_listening() const { return m_state->listening; }

private:
    struct state {
        fn::http::Server                         server;
        std::function<response(const request&)> handler;
        bool                                     has_handler = false;
        bool                                     listening   = false;
        bool                                     guarded     = false;
        guard                                    shield;
        std::string                              error;
    };

    std::shared_ptr<state> m_state;
};

class message {
public:
    message() = default;
    message(std::string d, bool binary, bool ok) : m_data(std::move(d)), m_binary(binary), m_ok(ok) {}

    const std::string& data() const { return m_data; }
    bool is_binary() const { return m_binary; }
    bool is_text() const { return !m_binary; }
    bool ok() const { return m_ok; }

private:
    std::string m_data;
    bool        m_binary = false;
    bool        m_ok     = false;
};

class websocket {
public:
    websocket() : m_state(std::make_shared<state>()) {}

    static websocket connect(const std::string& target, long long timeout_ms) {
        websocket w;
        auto st = w.m_state;
        fn::ws::ClientOptions opts;
        opts.handshake_timeout = std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 30000);
        st->sock = fn::ws::connect(target, opts, [st] { st->open = true; });
        attach(st);
        pump_until([st] { return st->open || st->closed || !st->error.empty(); }, (timeout_ms > 0 ? timeout_ms : 30000) + 1000);
        if (!st->open) fail("can't open a WebSocket to " + target + (st->error.empty() ? std::string() : ": " + st->error));
        return w;
    }

    static websocket adopt(fn::ws::WebSocket s) {
        websocket w;
        w.m_state->sock = std::move(s);
        w.m_state->open = true;
        attach(w.m_state);
        return w;
    }

    bool send_text(const std::string& text) {
        return m_state->sock && m_state->sock.send(text);
    }

    bool send_binary(const std::string& data) {
        return m_state->sock && m_state->sock.send(fn::DataView(data), true);
    }

    message receive(long long timeout_ms) {
        auto st = m_state;
        pump_until([st] {
            std::lock_guard<std::mutex> lock(st->mutex);
            return !st->inbox.empty() || st->closed;
        }, timeout_ms);
        std::lock_guard<std::mutex> lock(st->mutex);
        if (st->inbox.empty()) return message();
        message m = st->inbox.front();
        st->inbox.pop_front();
        return m;
    }

    template <typename F>
    void on_message(F callback) {
        m_state->callback = [callback](const message& m) mutable { callback(to_walnut<arg_of<F, 0>>(m)); };
    }

    void close(long long code, const std::string& reason) {
        if (m_state->sock) m_state->sock.close(static_cast<std::uint16_t>(code), reason);
        auto st = m_state;
        pump_until([st] { return st->closed; }, 2000);
    }

    bool is_open() const { return m_state->sock && m_state->sock.is_open(); }
    long long close_code() const { return m_state->close_code; }
    std::string close_reason() const { return m_state->close_reason; }
    std::string error() const { return m_state->error; }
    std::string remote_address() const { return m_state->sock ? m_state->sock.remote_address() : std::string(); }

private:
    struct state {
        fn::ws::WebSocket                    sock;
        bool                                 open   = false;
        bool                                 closed = false;
        long long                            close_code = 0;
        std::string                          close_reason;
        std::string                          error;
        std::mutex                           mutex;
        std::deque<message>                  inbox;
        std::function<void(const message&)>  callback;
    };

    static void attach(const std::shared_ptr<state>& st) {
        std::weak_ptr<state> weak = st;
        st->sock.on_message([weak](const fn::DataView& data, bool binary) {
            auto s = weak.lock();
            if (!s) return;
            message m(data.to_string(), binary, true);
            if (s->callback) {
                s->callback(m);
                return;
            }
            std::lock_guard<std::mutex> lock(s->mutex);
            s->inbox.push_back(std::move(m));
        });
        st->sock.on_close([weak](std::uint16_t code, const std::string& reason) {
            auto s = weak.lock();
            if (!s) return;
            s->closed       = true;
            s->open         = false;
            s->close_code   = code;
            s->close_reason = reason;
        });
        st->sock.on_error([weak](const std::error_code& ec) {
            auto s = weak.lock();
            if (s) s->error = error_text(ec);
        });
    }

    std::shared_ptr<state> m_state;
};

class websocket_server {
public:
    websocket_server() : m_state(std::make_shared<state>()) {}

    template <typename F>
    void handle(F on_connect) {
        m_state->server = fn::ws::create_server(fn::ws::ServerOptions(), [on_connect](fn::ws::WebSocket s, fn::http::IncomingMessage) mutable {
            on_connect(to_walnut<arg_of<F, 0>>(websocket::adopt(std::move(s))));
        });
        m_state->has_handler = true;
    }

    void listen(const std::string& host, long long port) {
        if (!m_state->has_handler) fail("give the WebSocket server a handler before calling listen()");
        auto st = m_state;
        st->server.listen(port_of(port), host, [st] { st->listening = true; });
        pump_until([st] { return st->listening; }, 5000);
        if (!st->listening) fail("the WebSocket server can't listen on " + host + ":" + std::to_string(port));
    }

    long long broadcast(const std::string& text) { return static_cast<long long>(m_state->server.broadcast(text)); }
    long long client_count() const { return static_cast<long long>(m_state->server.client_count()); }
    long long port() const { return static_cast<long long>(m_state->server.port()); }
    void serve() { fn::run(); }
    void serve_for(long long ms) { fn::run_for(std::chrono::milliseconds(ms < 0 ? 0 : ms)); }

    void close() {
        m_state->server.close();
        fn::run_for(std::chrono::milliseconds(20));
    }

private:
    struct state {
        fn::ws::Server server;
        bool           has_handler = false;
        bool           listening   = false;
    };

    std::shared_ptr<state> m_state;
};

class timer {
public:
    timer() = default;
    explicit timer(fn::Timer t) : m_timer(std::move(t)) {}

    void cancel() { m_timer.cancel(); }
    bool active() const { return m_timer.active(); }

private:
    fn::Timer m_timer;
};

template <typename F>
inline timer set_timeout(long long ms, F callback) {
    return timer(fn::set_timeout([callback]() mutable { callback(); }, std::chrono::milliseconds(ms < 0 ? 0 : ms)));
}

template <typename F>
inline timer set_interval(long long ms, F callback) {
    return timer(fn::set_interval([callback]() mutable { callback(); }, std::chrono::milliseconds(ms < 1 ? 1 : ms)));
}

template <typename F>
inline void next_tick(F callback) {
    fn::next_tick([callback]() mutable { callback(); });
}

inline long long run() { return static_cast<long long>(fn::run()); }
inline long long run_once() { return static_cast<long long>(fn::run_once()); }
inline long long poll() { return static_cast<long long>(fn::poll()); }
inline long long run_for(long long ms) { return static_cast<long long>(fn::run_for(std::chrono::milliseconds(ms < 0 ? 0 : ms))); }
inline void stop() { fn::stop(); }

inline std::string status_text(long long code) { return fn::http::status_text(static_cast<int>(code)); }
inline std::string url_encode(const std::string& s) { return fizmo::crypto::url_encode(s); }
inline std::string url_decode(const std::string& s) { return fizmo::crypto::url_decode(s); }

} // namespace net
} // namespace walnut_rt

#endif // WALNUT_NET_HPP
