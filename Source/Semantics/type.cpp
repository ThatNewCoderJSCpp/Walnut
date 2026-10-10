#include "Semantics/type.hpp"

namespace walnut {
namespace semantics {

std::uint8_t CV::bits() const {
    return std::uint8_t(is_const) | (std::uint8_t(is_volatile) << 1) | (std::uint8_t(is_immutable) << 2);
}

void Type::write_cv(std::ostream& os) const {
    if (m_cv.is_const)     os << "const ";
    if (m_cv.is_volatile)  os << "volatile ";
    if (m_cv.is_immutable) os << "immutable ";
}

int length_rank(parser_types::LengthModifier l) {
    using L = parser_types::LengthModifier;

    switch (l) {
        case L::ShortShort: return -2;
        case L::Short:      return -1;
        case L::Long:       return  1;
        case L::LongLong:   return  2;
        default:            return  0;   
    }
}

int builtin_width(
    parser_types::PrimitiveType::BaseKind base,
    parser_types::LengthModifier l,
    bool long_form
) {
    using BK = parser_types::PrimitiveType::BaseKind;
    const int r = length_rank(l);

    switch (base) {
        case BK::Float: return r + (long_form ? 4 : 2);   
        case BK::Int:   return r + (long_form ? 4 : 0);   
        case BK::Char:  return -2;                        
        case BK::Bool:  return -2;
        default:        return 0;
    }
}

const char* length_prefix(int rank) {
    switch (rank) {
        case -2: return "short short ";
        case -1: return "short ";
        case  1: return "long ";
        case  2: return "long long ";
        default: return "";
    }
}

const char* length_prefix(parser_types::LengthModifier length) {
    switch (length) {
        case parser_types::LengthModifier::ShortShort: return "short short ";
        case parser_types::LengthModifier::Short:      return "short ";
        case parser_types::LengthModifier::Long:       return "long ";
        case parser_types::LengthModifier::LongLong:   return "long long ";
        default: return "";
    }
}

std::string canonical_spelling(parser_types::PrimitiveType::BaseKind base, int w) {
    using BK = parser_types::PrimitiveType::BaseKind;

    switch (base) {
        case BK::Int:
            if (w >= -2 && w <= 2) return std::string(length_prefix(w))     + "int";
            if (w >=  3 && w <= 6) return std::string(length_prefix(w - 4)) + "integer";
            return "int<?>";

        case BK::Float:
            if (w >=  0 && w <= 4) return std::string(length_prefix(w - 2)) + "float";
            if (w >=  5 && w <= 6) return std::string(length_prefix(w - 4)) + "double";
            return "float<?>";

        case BK::Bool:    return "bool";
        case BK::Char:    return "char";
        case BK::String:  return "string";
        case BK::Text:    return "text";
        case BK::Void:    return "void";
        case BK::Auto:    return "auto";
        case BK::Dynamic: return "dynamic";
    }

    return "<unknown>";
}

BuiltinType::BuiltinType(BaseKind base, int width, bool is_unsigned, CV cv) : Type(TypeKind::Builtin, cv), m_base(base), m_width(std::int8_t(width)), m_unsigned(is_unsigned) {}

ArrayType::ArrayType(Type* element, std::optional<std::size_t> extent, CV cv, Symbol* extent_param) : Type(TypeKind::Array, cv), m_element(element), m_extent(extent), m_extent_param(extent_param) {}

bool TemplateArg::operator==(const TemplateArg& o) const {
    if (is_type != o.is_type || type != o.type || value != o.value || fvalue != o.fvalue) return false;
    if (param || o.param) return param == o.param;
    return expr == o.expr;
}

FunctionType::FunctionType(Type* ret, std::vector<Type*> params, bool is_const, RefQual rq, bool is_noexcept) : Type(TypeKind::Function), m_ret(ret), m_params(std::move(params)), m_const(is_const), m_ref(rq), m_noexcept(is_noexcept) {}

void FunctionType::write_to(std::ostream& os) const {
    os << '(';

    for (std::size_t i = 0; i < m_params.size(); ++i) {
        if (i) os << ", ";
        m_params[i]->write_to(os);
    }

    os << ") \u2192 ";
    m_ret->write_to(os);
    if (m_const)    os << " const";
    if (m_ref == RefQual::LValue) os << " &";
    else if (m_ref == RefQual::RValue) os << " &&";
    if (m_noexcept) os << " noexcept";
}

void VariantType::write_to(std::ostream& os) const {
    os << "variant(";

    for (std::size_t i = 0; i < m_alts.size(); ++i) {
        if (i) os << ", ";
        m_alts[i]->write_to(os);
    }
        
    os << ')';
}

void CoroutineType::write_to(std::ostream& os) const {
    write_cv(os);
    os << (m_is_generator ? "generator<" : "task<");
    if (m_value) m_value->write_to(os); else os << "?";
    os << ">";
}

std::size_t TypeContext::H::operator()(const UnaryKey& k) const {
    std::size_t h = std::size_t(k.kind);
    h = mix(h, std::hash<void*>{}(k.a)); h = mix(h, k.x);
    h = mix(h, k.has_n ? k.n + 1 : 0); return h;
}

std::size_t TypeContext::H::operator()(const RecordKey& k) const {
    std::size_t h = std::hash<void*>{}(k.decl); h = mix(h, k.cv);
    for (const TemplateArg& a : k.args) {
        h = mix(h, std::hash<const void*>{}(a.type));
        h = mix(h, std::hash<const void*>{}(a.value));
        h = mix(h, std::hash<const void*>{}(a.fvalue));
        h = mix(h, a.param ? std::hash<const void*>{}(a.param) : std::hash<const void*>{}(a.expr));
    }
    return h;
}

std::size_t TypeContext::H::operator()(const FuncKey& k) const {
    std::size_t h = std::hash<void*>{}(k.ret); h = mix(h, k.flags);
    for (Type* p : k.params) h = mix(h, std::hash<void*>{}(p)); return h;
}

} // namespace semantics
} // namespace walnut
