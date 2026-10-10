#ifndef PARSER_TYPE_QUALIFIERS_HPP
#define PARSER_TYPE_QUALIFIERS_HPP

#include <ostream>
#include <vector>
#include <string>
#include <cstdint>

namespace walnut {
namespace parser_types {

struct IndirectionQualifier {
    enum class Kind : std::uint8_t {
        Pointer,        // *
        Reference,      // &
        RValueReference // &&
    };

    Kind kind;
    bool is_const;  
    IndirectionQualifier(Kind k, bool c = false) : kind(k), is_const(c) {}
    bool is_pointer() const { return kind == Kind::Pointer; }
    bool is_reference()   const { return kind == Kind::Reference || kind == Kind::RValueReference; }
    bool is_lvalue_ref()  const { return kind == Kind::Reference; }
    bool is_rvalue_ref()  const { return kind == Kind::RValueReference; }

    void write_to(std::ostream& os) const;
    
    friend std::ostream& operator<<(std::ostream& os, IndirectionQualifier qual) {
        qual.write_to(os);
        return os;
    }
};

class IndirectionList {
private:
    std::vector<IndirectionQualifier> m_qualifiers;

public:
    IndirectionList() = default;
    IndirectionList(const IndirectionList&) = default;
    IndirectionList(IndirectionList&&) = default;
    IndirectionList& operator=(const IndirectionList&) = default;
    IndirectionList& operator=(IndirectionList&&) = default;
    void add_pointer(bool is_const = false) { m_qualifiers.emplace_back(IndirectionQualifier::Kind::Pointer, is_const); }
    void add_reference(bool is_const = false) { m_qualifiers.emplace_back(IndirectionQualifier::Kind::Reference, is_const); }
    void add_rvalue_reference(bool is_const = false) { m_qualifiers.emplace_back(IndirectionQualifier::Kind::RValueReference, is_const); }
    bool empty() const noexcept { return m_qualifiers.empty(); }
    std::size_t size() const noexcept { return m_qualifiers.size(); }
    const IndirectionQualifier& operator[](std::size_t index) const {  return m_qualifiers[index]; }
    const std::vector<IndirectionQualifier>& qualifiers() const noexcept { return m_qualifiers; }
    auto begin() const { return m_qualifiers.begin(); }
    auto end() const { return m_qualifiers.end(); }
    auto begin() { return m_qualifiers.begin(); }
    auto end() { return m_qualifiers.end(); }

    bool has_pointer() const {
        for (const auto& q : m_qualifiers) { if (q.is_pointer()) return true; }
        return false;
    }

    bool has_reference() const {
        for (const auto& q : m_qualifiers) { if (q.is_reference()) return true; }
        return false;
    }

    std::size_t pointer_depth() const;

    const IndirectionQualifier* back() const { return m_qualifiers.empty() ? nullptr : &m_qualifiers.back(); }
    void pop_back() { if (!m_qualifiers.empty()) { m_qualifiers.pop_back(); }}
    void clear() { m_qualifiers.clear(); }

    void write_to(std::ostream& os) const {
        for (const auto& q : m_qualifiers) {
            q.write_to(os);
        }
    }
    
    friend std::ostream& operator<<(std::ostream& os, const IndirectionList& list) {
        list.write_to(os);
        return os;
    }
};

} // namespace parser_types
} // namespace walnut

#endif // PARSER_TYPE_QUALIFIERS_HPP