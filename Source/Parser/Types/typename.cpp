#include "Parser/Types/typename.hpp"

namespace walnut {
namespace parser_types {

void PrimitiveType::write_to(std::ostream& os) const {
    if (m_is_unsigned) os << "unsigned ";
        
    switch (m_length) {
        case LengthModifier::ShortShort: os << "short short "; break;
        case LengthModifier::Short:      os << "short "; break;
        case LengthModifier::Long:       os << "long "; break;
        case LengthModifier::LongLong:   os << "long long "; break;
        default: break;
    }
        
    switch (m_base) {
        case BaseKind::Int:     os << (m_used_long_form ? "integer" : "int"); break;
        case BaseKind::Float:   os << (m_used_long_form ? "double" : "float"); break;
        case BaseKind::Bool:    os << "bool"; break;
        case BaseKind::Char:    os << "char"; break;
        case BaseKind::String:  os << "string"; break;
        case BaseKind::Text:    os << "text"; break;
        case BaseKind::Void:    os << "void"; break;
        case BaseKind::Auto:    os << "auto"; break;
        case BaseKind::Dynamic: os << "dynamic"; break;
    }
}

void UserDefinedType::write_to(std::ostream& os) const {
    if (m_is_global) os << "::";

    for (std::size_t i = 0; i < m_parts.size(); ++i) {
        if (i > 0) os << "::";
        os << m_parts[i];
    }
}

} // namespace parser_types
} // namespace walnut
