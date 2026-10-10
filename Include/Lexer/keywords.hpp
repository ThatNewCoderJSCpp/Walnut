#ifndef WALNUT_KEYWORDS_HPP
#define WALNUT_KEYWORDS_HPP

#include <string_view>
#include <cstdint>
#include <cstring>
#include <cassert>
#include "token_macro.hpp"

namespace walnut {
namespace detail {

inline constexpr std::size_t CAP = 256;

inline constexpr std::uint64_t wymum(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t a_lo = static_cast<std::uint32_t>(a);
    const std::uint64_t a_hi = a >> 32;
    const std::uint64_t b_lo = static_cast<std::uint32_t>(b);
    const std::uint64_t b_hi = b >> 32;
    const std::uint64_t p0 = a_lo * b_lo;
    const std::uint64_t p1 = a_lo * b_hi;
    const std::uint64_t p2 = a_hi * b_lo;
    const std::uint64_t p3 = a_hi * b_hi;
    const std::uint64_t cross = (p0 >> 32) + static_cast<std::uint32_t>(p1) + static_cast<std::uint32_t>(p2);
    const std::uint64_t lo = (p0 & 0xFFFFFFFFull) | (cross << 32);
    const std::uint64_t hi = p3 + (p1 >> 32) + (p2 >> 32) + (cross >> 32);
    return hi ^ lo;
}

inline constexpr std::uint64_t wyr8(const unsigned char* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) { v |= std::uint64_t(p[i]) << (8 * i); }
    return v;
}

inline constexpr std::uint64_t wyr4(const unsigned char* p) noexcept {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) { v |= std::uint32_t(p[i]) << (8 * i); }
    return v;
}

inline constexpr std::uint64_t wyr3(const unsigned char* p, std::size_t k) noexcept {
    std::uint64_t v = std::uint64_t(p[0]) << 16;
    v |= std::uint64_t(p[k >> 1]) << 8;
    v |= std::uint64_t(p[k - 1]);
    return v;
}

inline constexpr std::uint64_t WYP0 = 0xa0761d6478bd642full;
inline constexpr std::uint64_t WYP1 = 0xe7037ed1a0b428dbull;
inline constexpr std::uint64_t WYP2 = 0x8ebc6af09c88c6e3ull;
inline constexpr std::uint64_t WYP3 = 0x589965cc75374cc3ull;
inline constexpr std::uint64_t WYP4 = 0x1d8e4e27c47d124full;

std::uint64_t wyhash(std::string_view s, std::uint64_t seed = WYP0) noexcept;

inline constexpr std::size_t mix(std::uint64_t h, std::uint16_t d, std::size_t n) noexcept {
    std::uint64_t x = h + std::uint64_t(d) * 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x ^= (x >> 31);
    return static_cast<std::size_t>(x % n);
}

struct Slot {
    std::string_view        sp{};
    tokenizing::Token::Kind kind = tokenizing::Token::Kind::Unexpected;
    std::uint64_t           hash = 0;
};

struct Table {
    Slot          slots[CAP]{};
    std::uint16_t disp[CAP]{};
    std::size_t   n       = 0;
    std::size_t   min_len = 0;
    std::size_t   max_len = 0;
};

const Table& table() noexcept;

} // namespace detail

tokenizing::Token::Kind get_keyword(std::string_view s) noexcept;

inline bool is_keyword(std::string_view str) noexcept { return get_keyword(str) != tokenizing::Token::Kind::Unexpected; }
inline bool is_keyword(const tokenizing::Token& token) noexcept { return token.is_keyword(); }

} // namespace walnut

#endif // WALNUT_KEYWORDS_HPP