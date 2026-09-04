#ifndef PARSER_TYPE_INFO_HPP
#define PARSER_TYPE_INFO_HPP

#include "typename.hpp"
#include "type_qualifiers.hpp"
#include "../modifiers.hpp"
#include "../../Semantics/type.hpp"

namespace walnut {

namespace nodes { struct ASTNode; } 
namespace semantics { struct Symbol; }  

namespace parser_types {

struct TemplateArgument; 

class TypeInfo {
public:
    Typename type = nullptr;                         
    modifiers::RawModifiers modifiers; 
    IndirectionList indirection; 
    std::vector<TemplateArgument*> template_args;   
    TemplateArgument* alignment = nullptr;   
    semantics::Symbol* resolved = nullptr;  
    semantics::Type* canonical = nullptr;    

public:
    TypeInfo() = default;
    TypeInfo(Type* t) : type(t) {}
    TypeInfo(const TypeInfo& other) = default;
    TypeInfo& operator=(const TypeInfo& other) = default;
    TypeInfo(TypeInfo&& other) = default;
    TypeInfo& operator=(TypeInfo&& other) = default;
    TypeInfo clone_into(Arena& arena) const;

public:
    void add_pointer(bool is_const = false) { indirection.add_pointer(is_const); }
    void add_reference(bool is_const = false) { indirection.add_reference(is_const); }
    bool has_type() const { return type != nullptr; }
    bool has_modifiers() const { return modifiers.is_modified(); }
    bool has_indirection() const { return !indirection.empty(); }
    bool has_pointer() const { return indirection.has_pointer(); }
    bool has_reference() const { return indirection.has_reference(); }
    bool has_template_args() const { return !template_args.empty(); }
    bool has_alignment() const { return alignment != nullptr; }
    void write_to(std::ostream& os) const;

    friend std::ostream& operator<<(std::ostream& os, const TypeInfo& info) {
        info.write_to(os);
        return os;
    }
};

struct TemplateArgument {
    enum class Form : std::uint8_t { Type, Value };

    Form form = Form::Type;
    TypeInfo type;                       
    nodes::ASTNode* value = nullptr;     
    std::string_view value_repr;        
    bool is_pack = false;

    bool is_type()  const { return form == Form::Type; }
    bool is_value() const { return form == Form::Value; }

    void write_to(std::ostream& os) const {
        if (is_type()) {
            type.write_to(os);
        } else {
            os << (value_repr.empty() ? "<expr>" : value_repr);
        }

        if (is_pack) os << "...";
    }
};

inline void TypeInfo::write_to(std::ostream& os) const {
    if (has_modifiers()) {
        modifiers.write_to(os);
        os << ' ';
    }

    if (type) {
        type->write_to(os);
    } else {
        os << "<no type>";
    }

    if (has_template_args()) {
        os << '<';

        for (std::size_t i = 0; i < template_args.size(); ++i) {
            if (i > 0) os << ", ";
            template_args[i]->write_to(os);
        }

        os << '>';
    }

    if (has_indirection()) { indirection.write_to(os); }
}

inline TypeInfo TypeInfo::clone_into(Arena& arena) const {
    TypeInfo copy;
    copy.type = type ? type->clone_into(arena) : nullptr;
    copy.modifiers = modifiers;
    copy.indirection = indirection;
    copy.template_args.reserve(template_args.size());

    for (const TemplateArgument* a : template_args) {
        TemplateArgument* na = make_in<TemplateArgument>(arena);
        na->form       = a->form;
        na->type       = a->type.clone_into(arena);
        na->value      = a->value;       
        na->value_repr = a->value_repr;
        na->is_pack    = a->is_pack;
        copy.template_args.push_back(na);
    }

    return copy;
}

} // namespace parser_types
} // namespace walnut

#endif // PARSER_TYPE_INFO_HPP