#ifndef WALNUT_TRIE_HPP
#define WALNUT_TRIE_HPP

#include "walnut_collections.hpp"
#include "fizmo/../../Source/Collections/trie.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace walnut_rt {
namespace trie {

inline std::size_t limit_of(long long limit) {
    return limit < 0 ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(limit);
}

template <typename V>
class map {
    using cell = typename coll::cell_of<V>::type;
    using exact = fizmo::collections::TrieMap<cell, fizmo::collections::TrieExact>;
    using folded = fizmo::collections::TrieMap<cell, fizmo::collections::TrieIgnoreCase>;

    template <typename F>
    decltype(auto) with(F&& f) {
        return std::visit(std::forward<F>(f), m_trie);
    }

    template <typename F>
    decltype(auto) with(F&& f) const {
        return std::visit(std::forward<F>(f), m_trie);
    }

    static V* as_value(cell* c) noexcept { return reinterpret_cast<V*>(c); }
    static const V* as_value(const cell* c) noexcept { return reinterpret_cast<const V*>(c); }

public:
    map() = default;
    explicit map(bool ignore_case) {
        if (ignore_case) m_trie.template emplace<folded>();
    }

    bool ignores_case() const noexcept { return m_trie.index() == 1; }
    long long size() const { return with([](const auto& t) { return static_cast<long long>(t.size()); }); }
    bool empty() const { return with([](const auto& t) { return t.empty(); }); }
    void clear() { with([](auto& t) { t.clear(); }); }

    bool insert(const std::string& key, const V& value) { return with([&](auto& t) { return t.insert(key, cell(value)); }); }
    bool set(const std::string& key, const V& value) { return with([&](auto& t) { return t.insert_or_assign(key, cell(value)); }); }
    V& slot(const std::string& key) { return *with([&](auto& t) { return as_value(&t[key]); }); }
    V* find(const std::string& key) { return with([&](auto& t) { return as_value(t.find(key)); }); }
    const V* find(const std::string& key) const { return with([&](const auto& t) { return as_value(t.find(key)); }); }
    bool contains(const std::string& key) const { return with([&](const auto& t) { return t.contains(key); }); }
    bool remove(const std::string& key) { return with([&](auto& t) { return t.erase(key); }); }

    bool has_prefix(const std::string& prefix) const { return with([&](const auto& t) { return t.has_prefix(prefix); }); }
    long long count_prefix(const std::string& prefix) const { return with([&](const auto& t) { return static_cast<long long>(t.count_prefix(prefix)); }); }

    std::vector<std::string> keys_with_prefix(const std::string& prefix, long long limit) const {
        return with([&](const auto& t) { return t.keys_with_prefix(prefix, limit_of(limit)); });
    }

    std::vector<std::string> keys() const { return with([](const auto& t) { return t.keys(); }); }

    coll::vector<V> values_with_prefix(const std::string& prefix, long long limit) const {
        coll::vector<V> out;
        with([&](const auto& t) {
            for (const auto& [k, v] : t.entries_with_prefix(prefix, limit_of(limit))) out.push_back(*v);
        });
        return out;
    }

    std::string complete(const std::string& prefix) const { return with([&](const auto& t) { return t.complete(prefix); }); }

    long long longest_prefix_of(const std::string& text) const {
        return with([&](const auto& t) -> long long {
            if (auto hit = t.longest_prefix_of(text)) return static_cast<long long>(hit->first);
            return -1;
        });
    }

    std::vector<std::string> nearest(const std::string& word, long long max_distance, long long limit) const {
        return with([&](const auto& t) {
            std::vector<std::string> out;
            if (max_distance < 0) return out;
            for (auto& [k, d] : t.nearest(word, static_cast<std::size_t>(max_distance), limit_of(limit))) out.push_back(std::move(k));
            return out;
        });
    }

    std::vector<long long> nearest_distances(const std::string& word, long long max_distance, long long limit) const {
        return with([&](const auto& t) {
            std::vector<long long> out;
            if (max_distance < 0) return out;
            for (const auto& [k, d] : t.nearest(word, static_cast<std::size_t>(max_distance), limit_of(limit))) out.push_back(static_cast<long long>(d));
            return out;
        });
    }

private:
    std::variant<exact, folded> m_trie;
};

class set {
public:
    set() = default;
    explicit set(bool ignore_case) : m_map(ignore_case) {}

    bool ignores_case() const noexcept { return m_map.ignores_case(); }
    long long size() const { return m_map.size(); }
    bool empty() const { return m_map.empty(); }
    void clear() { m_map.clear(); }
    bool insert(const std::string& word) { return m_map.insert(word, true); }
    bool contains(const std::string& word) const { return m_map.contains(word); }
    bool remove(const std::string& word) { return m_map.remove(word); }
    bool has_prefix(const std::string& prefix) const { return m_map.has_prefix(prefix); }
    long long count_prefix(const std::string& prefix) const { return m_map.count_prefix(prefix); }
    std::vector<std::string> with_prefix(const std::string& prefix, long long limit) const { return m_map.keys_with_prefix(prefix, limit); }
    std::vector<std::string> words() const { return m_map.keys(); }
    std::string complete(const std::string& prefix) const { return m_map.complete(prefix); }
    long long longest_prefix_of(const std::string& text) const { return m_map.longest_prefix_of(text); }
    std::vector<std::string> nearest(const std::string& word, long long max_distance, long long limit) const { return m_map.nearest(word, max_distance, limit); }
    std::vector<long long> nearest_distances(const std::string& word, long long max_distance, long long limit) const { return m_map.nearest_distances(word, max_distance, limit); }

private:
    map<bool> m_map;
};

} // namespace trie
} // namespace walnut_rt

#endif // WALNUT_TRIE_HPP
