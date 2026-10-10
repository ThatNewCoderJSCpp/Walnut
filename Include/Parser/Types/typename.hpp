#ifndef PARSER_TYPENAME_HPP
#define PARSER_TYPENAME_HPP

#include <ostream>
#include <string>
#include <string_view>
#include <cstdint>
#include "../../Common/arena_allocator.hpp"

namespace walnut {
namespace parser_types {

enum class LengthModifier : std::uint8_t {
    None = 0,
    Undefined,
    Short,       
    ShortShort, 
    Long,        
    LongLong     
};

class Type {
public:
    enum class Kind : std::uint8_t {
        Primitive = 0,
        UserDefined,
        FunctionPointer,
        Array,
        Enum,
        EnumClass,
        Variant,
        Qualified
    };

    virtual ~Type() = default;
    virtual Kind kind() const = 0;
    virtual Type* clone_into(Arena& arena) const = 0;
    
    bool is_primitive() const { return kind() == Kind::Primitive; }
    bool is_user_defined() const { return kind() == Kind::UserDefined; }
    bool is_function_pointer() const { return kind() == Kind::FunctionPointer; }
    bool is_array() const { return kind() == Kind::Array; }
    bool is_enum() const { return kind() == Kind::Enum; }
    bool is_enum_class() const { return kind() == Kind::EnumClass; }
    bool is_variant() const { return kind() == Kind::Variant; }
    bool is_qualified() const { return kind() == Kind::Qualified; }

    virtual void write_to(std::ostream& os) const = 0;
};

class PrimitiveType : public Type {
public:
    enum class BaseKind : std::uint8_t {
        Int,        // int, integer
        Float,      // float, dec, decimal, floater, double
        Bool,       // bool, boolean
        Char,       // char, character
        String,     // string, str
        Text,       // text
        Void,       // void
        Auto,       // auto, automatic
        Dynamic,    // let, var, variable, def, define, any
    };

private:
    BaseKind m_base;
    LengthModifier m_length;
    bool m_is_unsigned;
    bool m_used_long_form; 

public:
    PrimitiveType(
        BaseKind base, 
        LengthModifier length = LengthModifier::None, 
        bool is_unsigned = false, 
        bool long_form = false
    ) 
        : m_base(base)
        , m_length(length)
        , m_is_unsigned(is_unsigned)
        , m_used_long_form(long_form) 
    {}

    Kind kind() const override { return Kind::Primitive; }
    
    Type* clone_into(Arena& arena) const override { 
        return make_in<PrimitiveType>(arena, m_base, m_length, m_is_unsigned, m_used_long_form);
    }
    
    void write_to(std::ostream& os) const override;

    BaseKind base_kind() const { return m_base; }
    LengthModifier length_modifier() const { return m_length; }
    bool is_unsigned() const { return m_is_unsigned; }
    bool used_long_form() const { return m_used_long_form; }
    
    bool is_integral() const { 
        return m_base == BaseKind::Int || m_base == BaseKind::Char || m_base == BaseKind::Bool; 
    }

    bool is_floating() const { return m_base == BaseKind::Float; }
    bool is_numeric() const { return is_integral() || is_floating(); }
    bool is_void() const { return m_base == BaseKind::Void; }
    bool is_auto() const { return m_base == BaseKind::Auto; }
    bool is_dynamic() const { return m_base == BaseKind::Dynamic; }

    static PrimitiveType* make_auto(Arena& arena) { 
        return make_in<PrimitiveType>(arena, BaseKind::Auto); 
    }
    
    static PrimitiveType* make_dynamic(Arena& arena) { 
        return make_in<PrimitiveType>(arena, BaseKind::Dynamic); 
    }
    
    static PrimitiveType* make_void(Arena& arena) { 
        return make_in<PrimitiveType>(arena, BaseKind::Void); 
    }
    
    static PrimitiveType* make_bool(Arena& arena) { 
        return make_in<PrimitiveType>(arena, BaseKind::Bool); 
    }
    
    static PrimitiveType* make_int(Arena& arena, bool is_unsigned = false) { 
        return make_in<PrimitiveType>(arena, BaseKind::Int, LengthModifier::None, is_unsigned); 
    }
    
    static PrimitiveType* make_float(Arena& arena) { 
        return make_in<PrimitiveType>(arena, BaseKind::Float); 
    }
    
    static PrimitiveType* make_double(Arena& arena) { 
        return make_in<PrimitiveType>(arena, BaseKind::Float, LengthModifier::Long, false, true); 
    }
    
    static PrimitiveType* make_char(Arena& arena) { 
        return make_in<PrimitiveType>(arena, BaseKind::Char); 
    }
    
    static PrimitiveType* make_string(Arena& arena) { 
        return make_in<PrimitiveType>(arena, BaseKind::String); 
    }
    
    static PrimitiveType* make_text(Arena& arena) { 
        return make_in<PrimitiveType>(arena, BaseKind::Text); 
    }
    
    static PrimitiveType* make_long(Arena& arena, bool is_unsigned = false) { 
        return make_in<PrimitiveType>(arena, BaseKind::Int, LengthModifier::Long, is_unsigned); 
    }
    
    static PrimitiveType* make_long_long(Arena& arena, bool is_unsigned = false) { 
        return make_in<PrimitiveType>(arena, BaseKind::Int, LengthModifier::LongLong, is_unsigned); 
    }
    
    static PrimitiveType* make_short(Arena& arena, bool is_unsigned = false) { 
        return make_in<PrimitiveType>(arena, BaseKind::Int, LengthModifier::Short, is_unsigned); 
    }
};

class UserDefinedType : public Type {
private:
    std::vector<std::string_view> m_parts;
    bool m_is_global = false;

public:
    explicit UserDefinedType(std::string_view name) { m_parts.push_back(name); }
    UserDefinedType(std::vector<std::string_view> parts, bool is_global = false) : m_parts(std::move(parts)), m_is_global(is_global) {}
    Kind kind() const override { return Kind::UserDefined; }

    Type* clone_into(Arena& arena) const override {
        return make_in<UserDefinedType>(arena, m_parts, m_is_global);
    }

    void write_to(std::ostream& os) const override;

    std::string_view name() const {
        return m_parts.empty() ? "" : m_parts.back();
    }

    const std::vector<std::string_view>& parts() const { return m_parts; }
    bool is_global() const { return m_is_global; }
    bool is_qualified() const { return m_parts.size() > 1 || m_is_global; }

    static UserDefinedType* make(Arena& arena, std::string_view name) {
        return make_in<UserDefinedType>(arena, name);
    }

    static UserDefinedType* make_qualified(
        Arena& arena,
        std::vector<std::string_view> parts,
        bool is_global = false
    ) {
        return make_in<UserDefinedType>(arena, std::move(parts), is_global);
    }
};

using Typename = Type*;

inline std::ostream& operator<<(std::ostream& os, const Type& t) {
    t.write_to(os);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Type* t) {
    if (t) t->write_to(os);
    else os << "<null>";
    return os;
}

} // namespace parser_types
} // namespace walnut

#endif // PARSER_TYPENAME_HPP