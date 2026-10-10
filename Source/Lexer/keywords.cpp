#include "Lexer/keywords.hpp"

namespace walnut {
namespace detail {

std::uint64_t wyhash(std::string_view s, std::uint64_t seed) noexcept {
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

const Table& table() noexcept {
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
} // namespace walnut

namespace walnut {

tokenizing::Token::Kind get_keyword(std::string_view s) noexcept {
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

} // namespace walnut
