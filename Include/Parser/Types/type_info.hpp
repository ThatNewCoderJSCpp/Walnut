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
    bool has_angle_args = false;

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
    void add_rvalue_reference(bool is_const = false) { indirection.add_rvalue_reference(is_const); }
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

    void write_to(std::ostream& os) const;
};

} // namespace parser_types
} // namespace walnut

#endif // PARSER_TYPE_INFO_HPP