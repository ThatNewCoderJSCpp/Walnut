#ifndef WALNUT_CRYPTO_HPP
#define WALNUT_CRYPTO_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../crypto.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace walnut_rt {
namespace crypt {

namespace fc = fizmo::crypto;

template <typename... T>
struct tag {};

struct api {};

template <typename D>
inline std::string raw(const D& d) { return std::string(reinterpret_cast<const char*>(d.data()), d.size()); }

inline std::string raw_bytes(const std::vector<std::uint8_t>& v) { return std::string(reinterpret_cast<const char*>(v.data()), v.size()); }

inline std::string sha1(const std::string& s) { return raw(fc::SHA1::hash(s)); }
inline std::string sha224(const std::string& s) { return raw(fc::SHA224::hash(s)); }
inline std::string sha256(const std::string& s) { return raw(fc::SHA256::hash(s)); }
inline std::string sha384(const std::string& s) { return raw(fc::SHA384::hash(s)); }
inline std::string sha512(const std::string& s) { return raw(fc::SHA512::hash(s)); }

inline std::string hmac(long long algo, const std::string& key, const std::string& data) {
    switch (algo) {
        case 0: return raw(fc::HMAC<fc::SHA1>::mac(key, data));
        case 1: return raw(fc::HMAC<fc::SHA224>::mac(key, data));
        case 3: return raw(fc::HMAC<fc::SHA384>::mac(key, data));
        case 4: return raw(fc::HMAC<fc::SHA512>::mac(key, data));
        default: return raw(fc::HMAC<fc::SHA256>::mac(key, data));
    }
}

inline std::string pbkdf2(long long algo, const std::string& password, const std::string& salt, long long iterations, long long length) {
    if (iterations <= 0) fail("pbkdf2 needs a positive number of iterations");
    if (length <= 0) fail("pbkdf2 needs a positive output length");
    const auto it  = static_cast<std::uint32_t>(iterations);
    const auto len = static_cast<std::size_t>(length);
    switch (algo) {
        case 0: return raw_bytes(fc::pbkdf2<fc::SHA1>(password.data(), password.size(), salt.data(), salt.size(), it, len));
        case 4: return raw_bytes(fc::pbkdf2<fc::SHA512>(password.data(), password.size(), salt.data(), salt.size(), it, len));
        default: return raw_bytes(fc::pbkdf2<fc::SHA256>(password.data(), password.size(), salt.data(), salt.size(), it, len));
    }
}

inline std::string hkdf(long long algo, const std::string& salt, const std::string& key, const std::string& info, long long length) {
    if (length <= 0) fail("hkdf needs a positive output length");
    const auto len = static_cast<std::size_t>(length);
    try {
        switch (algo) {
            case 4: return raw_bytes(fc::hkdf<fc::SHA512>(salt.data(), salt.size(), key.data(), key.size(), info.data(), info.size(), len));
            default: return raw_bytes(fc::hkdf<fc::SHA256>(salt.data(), salt.size(), key.data(), key.size(), info.data(), info.size(), len));
        }
    } catch (const std::exception& e) {
        fail(std::string("hkdf: ") + e.what());
    }
}

inline std::string to_hex(const std::string& s, bool upper) { return fc::to_hex(s, upper); }

inline std::string from_hex(const std::string& s) {
    std::vector<std::uint8_t> out;
    if (!fc::from_hex(s, out)) fail("'" + s + "' isn't valid hexadecimal");
    return raw_bytes(out);
}

inline bool is_hex(const std::string& s) {
    std::vector<std::uint8_t> out;
    return fc::from_hex(s, out);
}

inline std::string base64_encode(const std::string& s, bool pad) { return fc::base64_encode(s, pad); }
inline std::string base64url_encode(const std::string& s, bool pad) { return fc::base64url_encode(s, pad); }
inline std::string base32_encode(const std::string& s, bool pad) { return fc::base32_encode(s.data(), s.size(), pad); }

inline std::string base64_decode(const std::string& s) {
    std::vector<std::uint8_t> out;
    if (!fc::base64_decode(s, out)) fail("the text isn't valid base64");
    return raw_bytes(out);
}

inline std::string base64url_decode(const std::string& s) {
    std::vector<std::uint8_t> out;
    if (!fc::base64url_decode(s, out)) fail("the text isn't valid base64url");
    return raw_bytes(out);
}

inline std::string base32_decode(const std::string& s) {
    std::vector<std::uint8_t> out;
    if (!fc::base32_decode(s, out)) fail("the text isn't valid base32");
    return raw_bytes(out);
}

inline std::string url_encode(const std::string& s, bool plus) { return fc::url_encode(s, plus); }
inline std::string url_decode(const std::string& s, bool plus) { return fc::url_decode(s, plus); }

inline std::string random_token(long long bytes) { return fc::random_token(static_cast<std::size_t>(bytes > 0 ? bytes : 32)); }
inline std::string random_token_base64url(long long bytes) { return fc::random_token_base64url(static_cast<std::size_t>(bytes > 0 ? bytes : 32)); }
inline std::string uuid_v4() { return fc::uuid_v4(); }

inline std::string random_bytes(long long count) {
    if (count <= 0) return std::string();
    return from_hex(fc::random_token(static_cast<std::size_t>(count)));
}

inline bool secure_equals(const std::string& a, const std::string& b) { return fc::secure_equals(a, b); }

inline unsigned long long crc32(const std::string& s) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : s) c = table[(c ^ ch) & 0xFFu] ^ (c >> 8);
    return static_cast<unsigned long long>(c ^ 0xFFFFFFFFu);
}

inline unsigned long long fnv1a64(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char ch : s) {
        h ^= ch;
        h *= 1099511628211ULL;
    }
    return static_cast<unsigned long long>(h);
}

inline unsigned long long adler32(const std::string& s) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (unsigned char ch : s) {
        a = (a + ch) % 65521u;
        b = (b + a) % 65521u;
    }
    return static_cast<unsigned long long>((b << 16) | a);
}

class hasher {
public:
    hasher() = default;
    explicit hasher(long long algo) : m_algo(algo) {}

    static hasher make(long long algo) { return hasher(algo); }

    void update(const std::string& s) {
        switch (m_algo) {
            case 0: m_sha1.update(s); break;
            case 1: m_sha224.update(s); break;
            case 3: m_sha384.update(s); break;
            case 4: m_sha512.update(s); break;
            default: m_sha256.update(s); break;
        }
        m_count += static_cast<long long>(s.size());
    }

    std::string finish() {
        std::string out;
        switch (m_algo) {
            case 0: out = raw(m_sha1.finish()); m_sha1.reset(); break;
            case 1: out = raw(m_sha224.finish()); m_sha224.reset(); break;
            case 3: out = raw(m_sha384.finish()); m_sha384.reset(); break;
            case 4: out = raw(m_sha512.finish()); m_sha512.reset(); break;
            default: out = raw(m_sha256.finish()); m_sha256.reset(); break;
        }
        m_count = 0;
        return out;
    }

    void reset() {
        m_sha1.reset();
        m_sha224.reset();
        m_sha256.reset();
        m_sha384.reset();
        m_sha512.reset();
        m_count = 0;
    }

    long long bytes_hashed() const { return m_count; }
    long long algorithm() const { return m_algo; }

private:
    long long   m_algo  = 2;
    long long   m_count = 0;
    fc::SHA1    m_sha1;
    fc::SHA224  m_sha224;
    fc::SHA256  m_sha256;
    fc::SHA384  m_sha384;
    fc::SHA512  m_sha512;
};

class mac {
public:
    mac() = default;
    mac(long long algo, const std::string& key) : m_algo(algo), m_key(key) {}

    static mac make(long long algo, const std::string& key) { return mac(algo, key); }

    void update(const std::string& s) { m_data += s; }

    std::string finish() {
        const std::string out = hmac(m_algo, m_key, m_data);
        m_data.clear();
        return out;
    }

    void reset() { m_data.clear(); }

private:
    long long   m_algo = 2;
    std::string m_key;
    std::string m_data;
};

} // namespace crypt
} // namespace walnut_rt

#endif // WALNUT_CRYPTO_HPP
