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
    if (iterations <= 0 || iterations > 0xFFFFFFFFLL) fail("pbkdf2 needs from 1 to 4294967295 iterations");
    if (length <= 0) fail("pbkdf2 needs a positive output length");
    if (salt.size() < 8) fail("pbkdf2: the salt has to be at least 8 bytes; generate_salt() makes a 16-byte one");
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

inline fc::Argon2Params password_settings(long long memory_kib, long long iterations, long long parallelism, long long hash_length) {
    if (iterations < 1 || iterations > 0xFFFFFFFFLL) fail("password settings: the iterations have to be from 1 to 4294967295, not " + std::to_string(iterations));
    if (parallelism < 1 || parallelism > 255) fail("password settings: the parallelism has to be from 1 to 255, not " + std::to_string(parallelism));
    if (memory_kib < 8 * parallelism || memory_kib > 0xFFFFFFFFLL) fail("password settings: the memory has to be at least 8 KiB for each lane of parallelism (" + std::to_string(8 * parallelism) + " KiB here) and at most 4 TiB, not " + std::to_string(memory_kib) + " KiB");
    if (hash_length < 16 || hash_length > 1024) fail("password settings: the hash length has to be from 16 to 1024 bytes, not " + std::to_string(hash_length));
    fc::Argon2Params p;
    p.type = fc::Argon2Type::id;
    p.memory_kib = static_cast<std::uint32_t>(memory_kib);
    p.iterations = static_cast<std::uint32_t>(iterations);
    p.parallelism = static_cast<std::uint32_t>(parallelism);
    p.hash_length = static_cast<std::uint32_t>(hash_length);
    return p;
}

inline fc::Argon2Params password_settings(unsigned long long memory_kib, unsigned long long iterations, unsigned long long parallelism, unsigned long long hash_length, unsigned long long salt_length) {
    if (salt_length < 8 || salt_length > 1024) fail("password settings: the salt length has to be from 8 to 1024 bytes, not " + std::to_string(salt_length));
    const auto clip = [](unsigned long long v) { return static_cast<long long>(v > 0x7FFFFFFFFFFFFFFFULL ? 0x7FFFFFFFFFFFFFFFULL : v); };
    fc::Argon2Params p = password_settings(clip(memory_kib), clip(iterations), clip(parallelism), clip(hash_length));
    p.salt_length = static_cast<std::uint32_t>(salt_length);
    return p;
}

inline void check_pepper(const std::string& pepper, const char* what) {
    if (pepper.size() < 16) fail(std::string(what) + ": the pepper has to be at least 16 bytes, not " + std::to_string(pepper.size()) + "; generate_key() makes a 32-byte one");
}

inline fc::PasswordStrength strength_of(long long level) {
    switch (level) {
        case 0: return fc::PasswordStrength::minimum;
        case 2: return fc::PasswordStrength::moderate;
        case 3: return fc::PasswordStrength::sensitive;
        default: return fc::PasswordStrength::interactive;
    }
}

inline long long strength_memory(long long level) { return static_cast<long long>(fc::Argon2Params::of(strength_of(level)).memory_kib); }
inline long long strength_iterations(long long level) { return static_cast<long long>(fc::Argon2Params::of(strength_of(level)).iterations); }

template <typename Fn>
inline auto crypto_guard(Fn&& fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const std::bad_alloc&) {
        fail("not enough memory for the password settings; use less memory or a lower strength");
    } catch (const std::invalid_argument& e) {
        fail(e.what());
    }
}

inline std::string hash_password(const std::string& password, long long memory_kib, long long iterations, long long parallelism, long long hash_length) {
    const fc::Argon2Params p = password_settings(memory_kib, iterations, parallelism, hash_length);
    return crypto_guard([&] { return fc::hash_password(password, p); });
}

inline bool verify_password(const std::string& password, const std::string& encoded) {
    return crypto_guard([&] { return fc::verify_password(password, encoded); });
}

inline std::string hash_password_salted(const std::string& password, unsigned long long memory_kib, unsigned long long iterations, unsigned long long parallelism, unsigned long long hash_length, unsigned long long salt_length) {
    const fc::Argon2Params p = password_settings(memory_kib, iterations, parallelism, hash_length, salt_length);
    return crypto_guard([&] { return fc::hash_password(password, p); });
}

inline std::string hash_password_peppered(const std::string& password, const std::string& pepper, unsigned long long memory_kib, unsigned long long iterations, unsigned long long parallelism, unsigned long long hash_length, unsigned long long salt_length) {
    check_pepper(pepper, "hash_password_with_pepper");
    const fc::Argon2Params p = password_settings(memory_kib, iterations, parallelism, hash_length, salt_length);
    return crypto_guard([&] { return fc::hash_password(password, p, pepper); });
}

inline bool verify_password_peppered(const std::string& password, const std::string& encoded, const std::string& pepper) {
    check_pepper(pepper, "verify_password_with_pepper");
    return crypto_guard([&] { return fc::verify_password(password, encoded, pepper); });
}

inline std::string generate_salt(unsigned long long length) {
    if (length < 8 || length > 1024) fail("generate_salt: the length has to be from 8 to 1024 bytes, not " + std::to_string(length));
    return fc::generate_salt(static_cast<std::size_t>(length));
}

inline bool password_needs_rehash_salted(const std::string& encoded, unsigned long long memory_kib, unsigned long long iterations, unsigned long long parallelism, unsigned long long hash_length, unsigned long long salt_length) {
    return fc::password_needs_rehash(encoded, password_settings(memory_kib, iterations, parallelism, hash_length, salt_length));
}

inline std::string argon2id_secret(const std::string& password, const std::string& salt, const std::string& secret, unsigned long long memory_kib, unsigned long long iterations, unsigned long long parallelism, unsigned long long length) {
    if (salt.size() < 8) fail("argon2id: the salt has to be at least 8 bytes; generate_salt() makes a 16-byte one");
    if (length < 4 || length > 0xFFFFFFFFULL) fail("argon2id: the length has to be from 4 to 4294967295 bytes");
    fc::Argon2Params p = password_settings(memory_kib, iterations, parallelism, 32, 16);
    p.hash_length = static_cast<std::uint32_t>(length);
    return crypto_guard([&] { return raw_bytes(fc::argon2(p, password, salt, secret)); });
}

inline std::string derive_key_unsigned(const std::string& password, const std::string& salt, unsigned long long memory_kib, unsigned long long iterations, unsigned long long parallelism) {
    if (salt.size() < 8) fail("derive_key: the salt has to be at least 8 bytes; generate_salt() makes a 16-byte one");
    const fc::Argon2Params p = password_settings(memory_kib, iterations, parallelism, 32, 16);
    return crypto_guard([&] { return fc::derive_key(password, salt, p); });
}

inline std::string encrypt_with_password_unsigned(const std::string& password, const std::string& plaintext, unsigned long long memory_kib, unsigned long long iterations, unsigned long long parallelism, const std::string& associated) {
    const fc::Argon2Params p = password_settings(memory_kib, iterations, parallelism, 32, 16);
    return crypto_guard([&] { return fc::encrypt_with_password(password, plaintext, p, associated); });
}

inline unsigned long long strength_memory_unsigned(long long level) { return static_cast<unsigned long long>(fc::Argon2Params::of(strength_of(level)).memory_kib); }
inline unsigned long long strength_iterations_unsigned(long long level) { return static_cast<unsigned long long>(fc::Argon2Params::of(strength_of(level)).iterations); }

inline bool is_password_hash(const std::string& encoded) {
    fc::Argon2Params p;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> hash;
    return fc::argon2_decode(encoded, p, salt, hash);
}

inline bool password_needs_rehash(const std::string& encoded, long long memory_kib, long long iterations, long long parallelism, long long hash_length) {
    return fc::password_needs_rehash(encoded, password_settings(memory_kib, iterations, parallelism, hash_length));
}

inline std::string argon2id(const std::string& password, const std::string& salt, long long memory_kib, long long iterations, long long parallelism, long long length) {
    if (salt.size() < 8) fail("argon2id: the salt has to be at least 8 bytes; generate_salt() makes a 16-byte one");
    if (length < 4 || length > 0xFFFFFFFFLL) fail("argon2id: the length has to be at least 4 bytes");
    fc::Argon2Params p = password_settings(memory_kib, iterations, parallelism, 32);
    p.hash_length = static_cast<std::uint32_t>(length);
    return crypto_guard([&] { return raw_bytes(fc::argon2(p, password, salt)); });
}

inline std::string blake2b(const std::string& data, long long length, const std::string& key) {
    if (length < 1 || length > 64) fail("blake2b: the length has to be from 1 to 64 bytes, not " + std::to_string(length));
    if (key.size() > 64) fail("blake2b: the key can be at most 64 bytes, not " + std::to_string(key.size()));
    return raw_bytes(fc::BLAKE2b::hash(data, static_cast<std::size_t>(length), key));
}

inline std::string generate_key() { return fc::generate_key(); }

inline void check_key(const std::string& key, const char* what) {
    if (key.size() != 32) fail(std::string(what) + ": the key has to be 32 bytes, not " + std::to_string(key.size()) + "; make one with generate_key() or derive_key()");
}

inline std::string encrypt(const std::string& key, const std::string& plaintext, const std::string& associated) {
    check_key(key, "encrypt");
    return fc::encrypt(key, plaintext, associated);
}

inline bool decrypt_into(const std::string& key, const std::string& sealed, const std::string& associated, std::string& out) {
    check_key(key, "decrypt");
    return fc::decrypt(key, sealed, out, associated);
}

inline std::string derive_key(const std::string& password, const std::string& salt, long long memory_kib, long long iterations, long long parallelism) {
    if (salt.size() < 8) fail("derive_key: the salt has to be at least 8 bytes; generate_salt() makes a 16-byte one");
    const fc::Argon2Params p = password_settings(memory_kib, iterations, parallelism, 32);
    return crypto_guard([&] { return fc::derive_key(password, salt, p); });
}

inline std::string encrypt_with_password(const std::string& password, const std::string& plaintext, long long memory_kib, long long iterations, long long parallelism, const std::string& associated) {
    const fc::Argon2Params p = password_settings(memory_kib, iterations, parallelism, 32);
    return crypto_guard([&] { return fc::encrypt_with_password(password, plaintext, p, associated); });
}

inline bool decrypt_with_password_into(const std::string& password, const std::string& sealed, const std::string& associated, std::string& out) {
    return crypto_guard([&] { return fc::decrypt_with_password(password, sealed, out, associated); });
}

inline bool strequal(const std::string& a, const std::string& b) { return fc::strequal(a, b); }

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

    long long bytes_hashed() const noexcept { return m_count; }
    long long algorithm() const noexcept { return m_algo; }

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
