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

inline std::uint64_t wyhash(std::string_view s, std::uint64_t seed = WYP0) noexcept {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    std::size_t len = s.size();
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    seed ^= WYP0;

    if (len <= 16) {
        if (len >= 4) {
            a = (len > 8) ? wyr8(p) ^ WYP1 : (wyr4(p) ^ WYP1);
            b = (len > 8) ? wyr8(p + len - 8) ^ WYP2 : (wyr4(p + len - 4) ^ WYP2);
        } else if (len > 0) {
            a = wyr3(p, len) ^ WYP1;
            b = WYP2;
        } else {
            a = WYP1;
            b = WYP2;
        }

        return wymum(a ^ seed, b ^ WYP0);
    }

    std::size_t i = len;

    if (i > 48) {
        std::uint64_t see1 = seed, see2 = seed;
        do {
            seed = wymum(wyr8(p) ^ WYP1, wyr8(p + 8) ^ seed);
            see1 = wymum(wyr8(p + 16) ^ WYP2, wyr8(p + 24) ^ see1);
            see2 = wymum(wyr8(p + 32) ^ WYP3, wyr8(p + 40) ^ see2);
            p += 48;
            i -= 48;
        } while (i > 48);

        seed ^= see1 ^ see2;
    }

    while (i > 16) {
        seed = wymum(wyr8(p) ^ WYP1, wyr8(p + 8) ^ seed);
        p += 16;
        i -= 16;
    }

    a = wyr8(p + i - 16) ^ WYP1;
    b = wyr8(p + i - 8) ^ WYP2;
    return wymum(a ^ seed, b ^ (len ^ WYP0));
}

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

inline const Table& table() noexcept {
    static const Table t = [] {
        struct Pair { std::string_view s; tokenizing::Token::Kind k; };
        Pair all[CAP];
        std::size_t n  = 0;
        std::size_t lo = ~std::size_t(0);
        std::size_t hi = 0;

        for (std::size_t i = 0; i < std::size_t(tokenizing::Token::Kind::COUNT_); ++i) {
            const auto& info = tokenizing::Token::keyword_table_ptr()[i];
            if (!info.is_keyword) continue;

            for (std::size_t j = 0; j < info.spelling_count; ++j) {
                all[n++] = { info.spellings[j], tokenizing::Token::Kind(i) };
                auto len = info.spellings[j].size();
                if (len < lo) lo = len;
                if (len > hi) hi = len;
            }
        }

        std::size_t bsz[CAP]{}, bstart[CAP]{}, items[CAP]{}, cur[CAP]{};
        for (std::size_t i = 0; i < n; ++i) ++bsz[wyhash(all[i].s) % n];
        for (std::size_t i = 1; i < n; ++i) bstart[i] = bstart[i - 1] + bsz[i - 1];
        for (std::size_t i = 0; i < n; ++i) cur[i] = bstart[i];

        for (std::size_t i = 0; i < n; ++i) {
            auto b = wyhash(all[i].s) % n;
            items[cur[b]++] = i;
        }

        std::size_t order[CAP];
        for (std::size_t i = 0; i < n; ++i) order[i] = i;

        for (std::size_t i = 1; i < n; ++i) {
            auto key = order[i];
            auto j   = i;

            while (j > 0 && bsz[order[j - 1]] < bsz[key]) {
                order[j] = order[j - 1];
                --j;
            }

            order[j] = key;
        }

        Table tbl{};
        tbl.n       = n;
        tbl.min_len = lo;
        tbl.max_len = hi;
        bool used[CAP]{};
        std::size_t used_count = 0;

        for (std::size_t oi = 0; oi < n; ++oi) {
            auto bi = order[oi];
            auto bs = bsz[bi];
            if (bs == 0) continue;

            for (std::uint16_t d = 1;; ++d) {
                std::size_t pos[CAP];
                bool ok = true;

                for (std::size_t k = 0; k < bs; ++k) {
                    auto idx = items[bstart[bi] + k];
                    auto h   = wyhash(all[idx].s);
                    auto p   = mix(h, d, n);
                    if (used[p]) { ok = false; break; }
                    for (std::size_t q = 0; q < k; ++q) { if (pos[q] == p) { ok = false; break; }}
                    if (!ok) break;
                    pos[k] = p;
                }

                if (ok) {
                    tbl.disp[bi] = d;

                    for (std::size_t k = 0; k < bs; ++k) {
                        auto idx = items[bstart[bi] + k];
                        auto sp  = all[idx].s;
                        auto h   = wyhash(sp);
                        auto p   = pos[k];
                        tbl.slots[p] = { sp, all[idx].k, h };
                        used[p] = true;
                        ++used_count;
                    }

                    break;
                }
            }
        }

        assert(used_count == n);

        for (std::size_t i = 0; i < n; ++i) {
            auto sp = all[i].s;
            auto k  = all[i].k;
            auto h  = wyhash(sp);
            auto b  = h % n;
            auto d  = tbl.disp[b];
            auto si = mix(h, d, n);
            const auto& slot = tbl.slots[si];
            assert(slot.kind == k);
            assert(slot.sp.size() == sp.size());
            assert(std::memcmp(slot.sp.data(), sp.data(), sp.size()) == 0);
            assert(slot.hash == h);
        }

        return tbl;
    }();

    return t;
}

} // namespace detail

inline tokenizing::Token::Kind get_keyword(std::string_view s) noexcept {
    const auto& t = detail::table();
    std::uint64_t h = detail::wyhash(s);
    std::size_t len = s.size();
    if (len < t.min_len || len > t.max_len) return tokenizing::Token::Kind::Unexpected;
    auto b  = h % t.n;
    auto d  = t.disp[b];
    auto si = detail::mix(h, d, t.n);
    const auto& slot = t.slots[si];
    if (slot.hash != h) return tokenizing::Token::Kind::Unexpected;
    if (slot.sp.size() != s.size()) return tokenizing::Token::Kind::Unexpected;
    if (slot.sp.data()[0] != s[0]) return tokenizing::Token::Kind::Unexpected;
    if (std::memcmp(slot.sp.data(), s.data(), s.size()) != 0) return tokenizing::Token::Kind::Unexpected;
    return slot.kind;
}

inline bool is_keyword(std::string_view str) noexcept { return get_keyword(str) != tokenizing::Token::Kind::Unexpected; }
inline bool is_keyword(const tokenizing::Token& token) noexcept { return token.is_keyword(); }

} // namespace walnut

#endif // WALNUT_KEYWORDS_HPP