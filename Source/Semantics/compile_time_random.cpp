#include "Semantics/compile_time_random.hpp"

#include <cstddef>
#include <mutex>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <stdlib.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

namespace walnut {
namespace semantics {

namespace {

void os_random_bytes(void* buffer, std::size_t length) {
    if (length == 0) return;
#if defined(_WIN32)
    if (BCryptGenRandom(nullptr, static_cast<PUCHAR>(buffer), static_cast<ULONG>(length), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) throw std::runtime_error("BCryptGenRandom failed");
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(buffer, length);
#else
    auto* out = static_cast<unsigned char*>(buffer);
    std::size_t remaining = length;
#if defined(__linux__)
    while (remaining > 0) {
        const ssize_t got = ::getrandom(out, remaining, 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        out += got;
        remaining -= static_cast<std::size_t>(got);
    }
#endif
    if (remaining > 0) {
        const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (fd < 0) throw std::runtime_error("can't open /dev/urandom");
        while (remaining > 0) {
            const ssize_t got = ::read(fd, out, remaining);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) {
                ::close(fd);
                throw std::runtime_error("can't read /dev/urandom");
            }
            out += got;
            remaining -= static_cast<std::size_t>(got);
        }
        ::close(fd);
    }
#endif
}

struct Key {
    std::array<std::uint32_t, 8> words{};
    bool ready = false;
    bool seeded = false;
    std::string seed;
};

Key& key_state() {
    static Key k;
    return k;
}

std::mutex& key_mutex() {
    static std::mutex m;
    return m;
}

std::uint64_t splitmix(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::uint32_t rotl(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void quarter(std::array<std::uint32_t, 16>& s, int a, int b, int c, int d) {
    s[a] += s[b]; s[d] ^= s[a]; s[d] = rotl(s[d], 16);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = rotl(s[b], 12);
    s[a] += s[b]; s[d] ^= s[a]; s[d] = rotl(s[d], 8);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = rotl(s[b], 7);
}

std::array<std::uint32_t, 16> chacha20(const std::array<std::uint32_t, 16>& input) {
    std::array<std::uint32_t, 16> s = input;
    for (int i = 0; i < 10; ++i) {
        quarter(s, 0, 4, 8, 12); quarter(s, 1, 5, 9, 13); quarter(s, 2, 6, 10, 14); quarter(s, 3, 7, 11, 15);
        quarter(s, 0, 5, 10, 15); quarter(s, 1, 6, 11, 12); quarter(s, 2, 7, 8, 13); quarter(s, 3, 4, 9, 14);
    }
    for (std::size_t i = 0; i < 16; ++i) s[i] += input[i];
    return s;
}

const std::array<std::uint32_t, 8>& key_words() {
    std::lock_guard<std::mutex> lock(key_mutex());
    Key& k = key_state();
    if (k.ready) return k.words;

    if (k.seeded) {
        std::uint64_t h = 0xcbf29ce484222325ULL;
        for (unsigned char c : k.seed) { h ^= c; h *= 0x100000001b3ULL; }
        std::uint64_t x = h ^ static_cast<std::uint64_t>(k.seed.size());
        for (std::size_t i = 0; i < 8; i += 2) {
            const std::uint64_t v = splitmix(x);
            k.words[i] = static_cast<std::uint32_t>(v);
            k.words[i + 1] = static_cast<std::uint32_t>(v >> 32);
        }
    } else {
        os_random_bytes(k.words.data(), sizeof(std::uint32_t) * k.words.size());
    }

    k.ready = true;
    return k.words;
}

} // namespace

void set_compile_time_random_seed(const std::string& seed) {
    std::lock_guard<std::mutex> lock(key_mutex());
    Key& k = key_state();
    k.seed = seed;
    k.seeded = true;
    k.ready = false;
}

bool compile_time_random_is_seeded() {
    std::lock_guard<std::mutex> lock(key_mutex());
    return key_state().seeded;
}

RandomStream::RandomStream(std::uint64_t site, std::uint64_t place, std::uint64_t draw) {
    const std::array<std::uint32_t, 8>& key = key_words();
    m_input[0] = 0x61707865u;
    m_input[1] = 0x3320646eu;
    m_input[2] = 0x79622d32u;
    m_input[3] = 0x6b206574u;
    for (std::size_t i = 0; i < 8; ++i) m_input[4 + i] = key[i];
    std::uint64_t mix = site ^ (place * 0x9e3779b97f4a7c15ULL) ^ (draw * 0xc2b2ae3d27d4eb4fULL);
    const std::uint64_t a = splitmix(mix);
    const std::uint64_t b = splitmix(mix);
    m_input[12] = 0;
    m_input[13] = static_cast<std::uint32_t>(a);
    m_input[14] = static_cast<std::uint32_t>(a >> 32) ^ static_cast<std::uint32_t>(draw);
    m_input[15] = static_cast<std::uint32_t>(b) ^ static_cast<std::uint32_t>(b >> 32);
}

void RandomStream::refill() {
    m_block = chacha20(m_input);
    ++m_input[12];
    m_used = 0;
}

std::uint32_t RandomStream::next32() {
    if (m_used >= 16) refill();
    return m_block[m_used++];
}

std::uint64_t RandomStream::next64() {
    const std::uint64_t lo = next32();
    const std::uint64_t hi = next32();
    return lo | (hi << 32);
}

std::uint64_t RandomStream::below(std::uint64_t bound) {
    if (bound <= 1) return 0;
    const std::uint64_t threshold = (std::uint64_t(0) - bound) % bound;
    for (;;) {
        const std::uint64_t v = next64();
        if (v >= threshold) return v % bound;
    }
}

} // namespace semantics
} // namespace walnut
