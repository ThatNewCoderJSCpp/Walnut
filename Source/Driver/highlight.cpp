#include "Driver/highlight.hpp"
#include "Lexer/keywords.hpp"
#include "Semantics/compile_time_random.hpp"
#include "Parser/Nodes/node_children.hpp"
#include "Semantics/const_evaluator.hpp"
#include "Semantics/conversion.hpp"
#include "Semantics/instantiator.hpp"
#include "Semantics/scope.hpp"
#include "Semantics/symbol.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace walnut {
namespace driver {

namespace {

namespace N = nodes;
namespace S = semantics;
using K = N::ASTNode::Kind;

enum Modifier : std::uint32_t {
    Declaration       = 1u << 0,
    Overloaded        = 1u << 1,
    TemplateQualifier = 1u << 2,
    VariableTemplate  = 1u << 3
};

constexpr std::string_view modifier_names[] = { "declaration", "overloaded", "templateQualifier", "variableTemplate" };

const S::Symbol* follow(const S::Symbol* s) {
    for (int i = 0; s && i < 16; ++i) {
        if (s->kind == S::SymbolKind::Import && s->import_target) { s = s->import_target; continue; }
        if (s->merged_into && s->merged_into != s) { s = s->merged_into; continue; }
        break;
    }
    return s;
}

bool is_variable_template(const S::Symbol* s) {
    s = follow(s);
    return s && s->kind == S::SymbolKind::Function && s->decl && s->decl->kind == K::FunctionDeclaration && static_cast<const N::FunctionDeclaration*>(s->decl)->variable_template;
}

bool in_record(const S::Symbol* s) {
    return s && s->owner && s->owner->kind == S::Scope::Kind::Record;
}

std::string_view kind_of(const S::Symbol* s) {
    s = follow(s);
    if (!s) return {};

    switch (s->kind) {
        case S::SymbolKind::Variable:      return in_record(s) ? "property" : "variable";
        case S::SymbolKind::Parameter:     return "parameter";
        case S::SymbolKind::Function:
            if (s->decl && s->decl->kind == K::OperatorFunctionDeclaration) return "operator";
            if (is_variable_template(s)) return in_record(s) ? "property" : "variable";
            return in_record(s) ? "method" : "function";
        case S::SymbolKind::Type:
            if (s->decl && s->decl->kind == K::RecordDeclaration && static_cast<const N::RecordDeclaration*>(s->decl)->m_form != N::RecordDeclaration::Form::Class) return "struct";
            return "class";
        case S::SymbolKind::TypeAlias:     return "type";
        case S::SymbolKind::Enum:          return "enum";
        case S::SymbolKind::EnumConstant:  return "enumMember";
        case S::SymbolKind::Namespace:
        case S::SymbolKind::Module:        return "namespace";
        case S::SymbolKind::Concept:       return "concept";
        case S::SymbolKind::TemplateParam: return "typeParameter";
        case S::SymbolKind::Import:        return "namespace";
    }

    return {};
}

bool word_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

std::string json_string(std::string_view s) {
    std::string out = "\"";

    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }

    return out + "\"";
}

std::string type_text(S::Type* t) {
    return t ? S::type_str_of(t) : std::string();
}

std::string type_text(const parser_types::TypeInfo& t, S::TypeContext* types = nullptr) {
    if (t.canonical) return S::type_str_of(t.canonical);
    if (!t.has_type()) return std::string();

    if (types) {
        S::Type* c = types->canonicalize(t);
        const std::string text = type_text(c);
        if (c && text.find("<error") == std::string::npos) return text;
    }

    std::ostringstream os;
    t.write_to(os);
    return os.str();
}

std::string string_literal(std::string_view s) {
    std::string out = "\"";

    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '$':  out += "\\$";  break;
            default:   out += c;
        }
    }

    return out + "\"";
}

unsigned float_bits(S::Type* t) {
    if (!t || !t->is_builtin()) return 0;
    const auto* b = static_cast<S::BuiltinType*>(t);
    if (b->base() != parser_types::PrimitiveType::BaseKind::Float) return 0;
    return bit_width_of_rank(b->width());
}

std::string float_text(const WideFloat& f, unsigned bits) {
    const unsigned binary = bits ? float_digits_of_bits(bits) : 0;
    const long long most = binary ? static_cast<long long>(binary * 0.30103) + 3 : 17;

    for (long long digits = 1; digits < most; ++digits) {
        const std::string text = f.to_string(digits);
        bool ok = false;
        const WideFloat back = wf_parse(text, ok);
        if (!ok) continue;
        WideFloat rounded = back;
        if (bits && !wf_round_to_width(back, bits, rounded)) rounded = back;
        if (wf_same_bits(rounded, f)) return text;
    }

    return f.to_string(most);
}

std::string value_text(const S::ConstValue& v, S::Type* t) {
    switch (v.kind) {
        case S::ConstValue::Kind::Int: {
            if (!v.i) return std::string();
            std::string text = v.i->to_string();
            const std::string ts = type_text(t);

            if ((ts == "char" || ts == "const char") && !v.i->is_negative() && S::ConstTable::fits(*v.i, 7, false)) {
                const long long code = std::stoll(text);
                if (code >= 0x20 && code < 0x7F && code != '\'' && code != '\\') return "'" + std::string(1, static_cast<char>(code)) + "' (" + text + ")";
            }

            return text;
        }
        case S::ConstValue::Kind::Float:   return v.f ? float_text(*v.f, float_bits(t)) : std::string();
        case S::ConstValue::Kind::Bool:    return v.b ? "true" : "false";
        case S::ConstValue::Kind::Special: return S::special_spelling(v.sp);
        case S::ConstValue::Kind::String:  return v.s ? string_literal(*v.s) : std::string();
        case S::ConstValue::Kind::None:    break;
    }

    return std::string();
}

std::string joined(const std::vector<std::string_view>& parts) {
    std::string out;

    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += "::";
        out += parts[i];
    }

    return out;
}

std::string without_const(std::string t) {
    if (t.rfind("const ", 0) == 0) t.erase(0, 6);
    return t;
}

std::string prefix(const modifiers::RawModifiers& m) {
    using RM = modifiers::RawModifiers;
    std::string out;
    if (m.has(RM::Static))    out += "static ";
    if (m.has(RM::Inline))    out += "inline ";
    if (m.has(RM::Lazy))      out += "lazy ";
    if (m.has(RM::Constexpr)) out += "constexpr ";
    return out;
}

std::string iterable_text(const N::FunctionParameter* p, S::TypeContext* types);

std::string default_after(std::string_view name, bool angle) {
    if (name.empty() || !name.data()) return std::string();
    const char* p = name.data() + name.size();
    const char* limit = p + 400;
    while (p < limit && *p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= limit || *p != '=' || p[1] == '=') return std::string();
    ++p;
    while (p < limit && *p && (*p == ' ' || *p == '\t')) ++p;
    const char* start = p;
    int depth = 0;
    int angles = 0;
    char quote = 0;
    for (; p < limit && *p; ++p) {
        const char c = *p;
        if (quote) {
            if (c == '\\' && p[1]) ++p;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
        if ((c == ')' || c == ']' || c == '}') && depth > 0) { --depth; continue; }
        if (angle && c == '<') { ++angles; continue; }
        if (angle && c == '>' && angles > 0) { --angles; continue; }
        if (depth == 0 && angles == 0 && (c == ',' || c == ';' || c == ')' || c == ']' || c == '}' || c == '\n' || (angle && c == '>'))) break;
    }
    std::string out(start, static_cast<std::size_t>(p - start));
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r')) out.pop_back();
    if (out.size() > 80) out = out.substr(0, 77) + "...";
    return out;
}

std::string default_text(std::string_view name, bool angle) {
    const std::string text = default_after(name, angle);
    return text.empty() ? std::string("...") : text;
}

std::string signature(const N::FunctionDeclaration* f, S::TypeContext* types = nullptr) {
    if (f->variable_template) {
        std::string t = type_text(f->m_return_type, types);
        if (f->m_modifiers.has(modifiers::RawModifiers::Constexpr)) t = without_const(t);
        return prefix(f->m_modifiers) + (t.empty() ? std::string() : t + ": ") + joined(f->m_name_parts);
    }

    std::string out = prefix(f->m_modifiers) + "function " + joined(f->m_name_parts) + "(";

    if (f->m_parameters) {
        bool first = true;

        for (const N::FunctionParameter* p : f->m_parameters->m_params) {
            if (!p) continue;
            if (!first) out += ", ";
            first = false;
            out += p->is_iterable ? iterable_text(p, types) : type_text(p->m_type, types);

            if (p->is_variadic()) {
                out += "...";
                if (p->m_variadic_length != static_cast<std::size_t>(-1) && p->m_variadic_length > 1) out += "[" + std::to_string(p->m_variadic_length) + "]";
            }

            if (!p->m_name.empty()) out += ": " + std::string(p->m_name);
            if (p->m_initializer) out += " = " + default_text(p->m_name, false);
        }
    }

    out += ")";
    if (f->m_qualifiers.has(modifiers::FunctionQualifiers::Const)) out += " const";
    if (f->m_qualifiers.has(modifiers::FunctionQualifiers::Noexcept)) out += " noexcept";
    const std::string ret = type_text(f->m_return_type, types);
    if (!ret.empty()) out += " -> " + ret;
    return out;
}

std::string iterable_text(const N::FunctionParameter* p, S::TypeContext* types) {
    const std::string out = "iterable<" + type_text(p->iterable_element, types) + ">";
    const bool mutable_ref = p->m_type.has_reference() && !p->m_type.modifiers.has(modifiers::RawModifiers::Const);
    return mutable_ref ? out + "&" : out;
}

std::string params_text(const N::FunctionParameters* ps, S::TypeContext* types) {
    std::string out;
    if (!ps) return out;
    bool first = true;

    for (const N::FunctionParameter* p : ps->m_params) {
        if (!p) continue;
        if (!first) out += ", ";
        first = false;
        out += p->is_iterable ? iterable_text(p, types) : type_text(p->m_type, types);

        if (p->is_variadic()) {
            out += "...";
            if (p->m_variadic_length != static_cast<std::size_t>(-1) && p->m_variadic_length > 1) out += "[" + std::to_string(p->m_variadic_length) + "]";
        }

        if (!p->m_name.empty()) out += ": " + std::string(p->m_name);
        if (p->m_initializer) out += " = " + default_text(p->m_name, false);
    }

    return out;
}

std::string template_default(const N::TemplateParameter* p, S::TypeContext* types) {
    if (!p) return std::string();
    if (p->m_has_default_type) {
        const std::string t = type_text(p->m_default_type, types);
        if (!t.empty()) return " = " + t;
    }
    if (p->m_default_value || p->m_has_default_type) return " = " + default_text(p->m_name, true);
    return std::string();
}

std::string template_parameter_text(const N::TemplateParameter* p, S::TypeContext* types) {
    std::string out = p->is_type_param() ? std::string("typename") : type_text(p->m_type, types);
    if (p->m_is_pack) out += "...";
    if (!p->m_name.empty()) out += " " + std::string(p->m_name);
    return out + template_default(p, types);
}

std::string template_prefix(const S::Symbol* s, S::TypeContext* types) {
    if (!s || !s->template_decl) return std::string();
    std::string out = "template <";
    bool first = true;

    for (const N::TemplateParameter* p : s->template_decl->m_params) {
        if (!p || p->m_name.rfind("iterable<", 0) == 0) continue;
        if (!first) out += ", ";
        first = false;
        if (p->is_type_param()) out += "typename";
        else out += type_text(p->m_type, types);
        if (p->m_is_pack) out += "...";
        if (!p->m_name.empty()) out += " " + std::string(p->m_name);
        out += template_default(p, types);
    }

    if (first) return std::string();
    return out + "> ";
}

std::string operator_spelling(const N::OperatorFunctionDeclaration* o, S::TypeContext* types) {
    if (o->is_conversion()) return type_text(o->conversion_type, types);
    std::string out;

    if (o->at && std::string_view(o->at, 8) == "operator") {
        const char* p = o->at + 8;
        while (*p == ' ' || *p == '\t') ++p;

        if (*p == '(') {
            int depth = 1;
            const char* start = ++p;

            for (; *p && *p != '\n'; ++p) {
                if (*p == '(') ++depth;
                if (*p == ')' && --depth == 0) break;
            }

            if (*p == ')') {
                for (const char* q = start; q < p; ++q) if (*q != ' ' && *q != '\t') out += *q;
                if (!out.empty()) return out;
            }
        }
    }

    for (tokenizing::Token::Kind k : o->op_tokens) {
        const tokenizing::Token t(k, 0);
        if (t.spelling_count()) out += std::string(t.spellings()[0]);
    }

    return out;
}

std::string record_name_of(const S::Symbol* s) {
    if (!s || !s->owner || !s->owner->owner_symbol) return std::string();
    return std::string(s->owner->owner_symbol->name);
}

std::string function_signature(const S::Symbol* s, S::TypeContext* types) {
    s = follow(s);
    if (!s || !s->decl) return std::string();

    if (s->decl->kind == K::FunctionDeclaration) return template_prefix(s, types) + signature(static_cast<const N::FunctionDeclaration*>(s->decl), types);

    if (s->decl->kind == K::ConstructorDeclaration) {
        const auto* c = static_cast<const N::ConstructorDeclaration*>(s->decl);
        const std::string owner = record_name_of(s);
        return template_prefix(s, types) + "constructor" + (owner.empty() ? std::string() : " " + owner) + "(" + params_text(c->m_parameters, types) + ")";
    }

    if (s->decl->kind == K::OperatorFunctionDeclaration) {
        const auto* o = static_cast<const N::OperatorFunctionDeclaration*>(s->decl);
        std::string out = template_prefix(s, types) + "operator(" + operator_spelling(o, types) + ") function(" + params_text(o->m_parameters, types) + ")";
        if (o->m_qualifiers.has(modifiers::FunctionQualifiers::Const)) out += " const";
        const std::string ret = type_text(o->m_return_type, types);
        if (!ret.empty() && !o->is_conversion()) out += " -> " + ret;
        return out;
    }

    return std::string();
}

std::string_view decl_name(const N::ASTNode* d) {
    if (!d) return {};

    switch (d->kind) {
        case K::FunctionDeclaration: {
            const auto* f = static_cast<const N::FunctionDeclaration*>(d);
            return f->m_name_parts.empty() ? std::string_view() : f->m_name_parts.back();
        }
        case K::VariableDeclaration:  return static_cast<const N::VariableDeclaration*>(d)->m_name;
        case K::ArrayDeclaration:     return static_cast<const N::ArrayDeclaration*>(d)->m_name;
        case K::FunctionParameter:    return static_cast<const N::FunctionParameter*>(d)->m_name;
        case K::RecordDeclaration: {
            const auto* r = static_cast<const N::RecordDeclaration*>(d);
            return r->m_name_parts.empty() ? std::string_view() : r->m_name_parts.back();
        }
        case K::EnumDeclaration:      return static_cast<const N::EnumDeclaration*>(d)->name;
        case K::EnumValue:            return static_cast<const N::EnumValue*>(d)->name;
        case K::NamespaceDeclaration: {
            const auto* n = static_cast<const N::NamespaceDeclaration*>(d);
            return n->m_name_parts.empty() ? std::string_view() : n->m_name_parts.back();
        }
        case K::TemplateParameter:    return static_cast<const N::TemplateParameter*>(d)->m_name;
        case K::UsingDeclaration:     return static_cast<const N::UsingDeclaration*>(d)->name;
        case K::ConceptDeclaration:   return static_cast<const N::ConceptDeclaration*>(d)->name;
        case K::ForEachStatement:     return static_cast<const N::ForEachStatement*>(d)->m_var_name;
        case K::TemplateDeclaration:  return decl_name(static_cast<const N::TemplateDeclaration*>(d)->m_declaration);
        default: break;
    }

    return {};
}

using TK = tokenizing::Token::Kind;

struct KeywordDoc {
    TK          kind;
    const char* category;
    const char* syntax;
    const char* doc;
};

constexpr KeywordDoc k_keyword_docs[] = {
    { TK::ArrayKeyword, "keyword", "array[3] int: values = {7, 8, 9};", "Declares a fixed-size array; the size is part of the type, written `array[N] T`. `[]` or `[...]` takes the size from the initializer, and multiple dimensions nest the keyword (`array[2] array[3] int`)." },
    { TK::AsyncKeyword, "keyword", "function compute(int: x) async -> int { ... } end function compute", "Function qualifier, written after the parameter list, that makes the function a coroutine returning `task<T>`. The task starts running when it is awaited; `main` can't be `async`." },
    { TK::AsKeyword, "operator", "v as int", "Gives a variant's current alternative, a `var`'s value converted to `T`, or a polymorphic object as a reference or pointer to a derived class. Stops the program with a runtime error if the value isn't a `T`, so check with `is` first." },
    { TK::AwaitKeyword, "operator", "int: a = await compute(x);", "Waits for a `task<T>` and gives its result. It can be used in any function, including `main`." },
    { TK::AlignofKeyword, "operator", "alignof(T)", "Gives the alignment of a type in bytes, evaluated at compile time. Also usable as a method: `x.alignof()`." },
    { TK::AlignasKeyword, "keyword", "alignas(16) int: shared = 0;", "Places a variable, field, array or struct at an address that is a multiple of `N` bytes. `N` must be a power of two, given as a number, a constant or a type; on an array it goes right after `array`." },
    { TK::BitCastKeyword, "operator", "bit_cast<T>(value)", "Copies the exact bits of a value into another type of the same size. It can't turn a number into a `char` or `bool`." },
    { TK::BreakKeyword, "keyword", "break;", "Leaves the innermost loop, or ends a `switch` case. `break label;` leaves the labelled loop instead." },
    { TK::CaseKeyword, "keyword", "case 1: score = 1; break;", "Labels one value inside a `switch`. Case values can be integers, characters, enum values, or `string`/`text` literals." },
    { TK::ClassKeyword, "keyword", "class Name { ... } end class Name", "Declares a class, closed with `end class Name`. Members are `private` until an access section says otherwise." },
    { TK::ContinueKeyword, "keyword", "continue;", "Skips to the next iteration of the innermost loop. `continue label;` acts on the labelled loop instead." },
    { TK::CatchKeyword, "keyword", "catch (const T&: e) { ... }", "Handles exceptions of a given type after a `try` block. `catch (...)` catches anything and must come last." },
    { TK::ConceptKeyword, "keyword", "concept Addable = requires (T: a, T: b) { a + b; }", "Defines a named requirement on a type, used in place of `typename` or in a `requires` clause. The body can be a `requires` block, a trait, another concept, or a combination with `&&`, `||` and `!`; it has no `;` after it." },
    { TK::ConstexprKeyword, "keyword", "constexpr int: size = 4;", "Marks a variable as a compile-time constant (its initializer must be constant) or a function as able to run while compiling. `if constexpr` keeps only the branch whose condition is true." },
    { TK::ConstevalKeyword, "keyword", "", "Function qualifier, written after the parameter list, meaning the function must always be evaluated at compile time." },
    { TK::ConstinitKeyword, "keyword", "", "Variable modifier meaning the variable is initialized at compile time. It needs `static` or `thread_local`, and can't be combined with `constexpr` or `inline`." },
    { TK::ConstructorKeyword, "keyword", "constructor(int: x, string: y) : v = x, s = y { }", "Declares a constructor inside a class, with an optional initializer list after `:`. Outside the class it is defined as `constructor ClassName(...) { }`; `= default` and `= delete` are allowed." },
    { TK::ConstCastKeyword, "operator", "const_cast<int*>(read_only)", "Removes or adds `const` or `volatile` through a pointer or reference; only those qualifiers may differ between the two types. Writing through the result is only safe if the original object wasn't declared `const`." },
    { TK::CountOfKeyword, "operator", "countof(x)", "Gives the number of elements of an array, characters (code points) of a string, enumerators of an enum, or arguments in a variadic pack." },
    { TK::CoAwaitKeyword, "operator", "int: a = co_await compute(x);", "Waits for a `task<T>` and gives its result, the same as `await`." },
    { TK::CoReturnKeyword, "keyword", "co_return;", "Ends a generator coroutine early, the same as a bare `return;`." },
    { TK::CoYieldKeyword, "keyword", "co_yield i;", "Produces the next value from a function that returns `generator<T>`; the caller loops over it with `for ... in`." },
    { TK::DoKeyword, "keyword", "do { ... } while (condition);", "Starts a `do ... while` loop, whose body runs once before the condition is checked." },
    { TK::DefaultKeyword, "keyword", "default: score = 100;", "Marks the branch of a `switch` taken when no `case` matches. After a constructor, destructor or operator, `= default` asks for the compiler-generated version." },
    { TK::DeleteKeyword, "operator", "delete p;", "Frees an object created with `new`; `delete[] p` frees an array made with `new T[n]`. Written `= delete;` after a constructor, destructor or operator, it forbids that member." },
    { TK::DecltypeKeyword, "operator", "decltype(rx): alias = x;", "Names the declared type of a name, or `T&` for other lvalues, and is used wherever a type is written. Using it as a value is an error; use `typeid` for a type's name." },
    { TK::DownKeyword, "keyword", "", "Reserved keyword; not described in the documentation. It can still be used as a function or method name." },
    { TK::DestructorKeyword, "keyword", "destructor() { ... }", "Declares the destructor, which runs when the object goes out of scope or is deleted. Destructors are `noexcept`, and are virtual automatically in a class with virtual methods." },
    { TK::DynamicKeyword, "type", "", "Reserved word; the documentation calls `var` values dynamic but does not describe `dynamic` itself." },
    { TK::DynamicCastKeyword, "operator", "dynamic_cast<Square*>(p)", "Converts a pointer or reference to a class with virtual functions into a derived class, checked while the program runs. A failed pointer cast gives `nullptr`." },
    { TK::DiscardConstKeyword, "operator", "discard_const(typeof(cx)): changeable = 1;", "A compile-time type operation that gives `T` without `const`." },
    { TK::DiscardNodiscardKeyword, "operator", "discard_nodiscard(checked(2));", "Calls a `nodiscard` function while ignoring its result, without the warning." },
    { TK::ElseKeyword, "keyword", "} else if (x > 3) then { ... } else then { ... }", "Adds another branch to an `if`. Every `else` branch needs `then` before its block." },
    { TK::EndKeyword, "keyword", "} end function name", "Closes a named declaration: `end <kind> <name>` for functions, classes, structs, unions and namespaces, with the name matching the opening one, and `end extern` for extern blocks. `if`, loops, `switch`, `enum` and lambdas don't use it." },
    { TK::EnumKeyword, "keyword", "enum Color { Red, Green = 5, Blue }", "Declares an enum. Enums are always scoped (`Color::Blue`), don't convert to or from integers implicitly, and default to an `int` underlying type, set with `: type`." },
    { TK::ExportKeyword, "keyword", "export name", "Chooses what other files can import: a single name, `name as other`, `namespace n`, `this` for every global declaration, or `{ ... }` for several." },
    { TK::ExtendsKeyword, "keyword", "class Square extends Shape { ... } end class Square", "Names the base class in a class heading, the same as `inherits`." },
    { TK::ExternKeyword, "keyword", "extern \"C\" function puts(string: line) -> int;", "Declares a function or variable defined in another file, or with `\"C\"`/`\"C++\"` a native function or class. Several declarations can share an `extern { ... } end extern` block." },
    { TK::ExplicitKeyword, "keyword", "constructor(int: side) explicit : Rect(side, side) { }", "Stops a constructor from being used for brace-list conversions, or makes a conversion operator usable only through `static_cast`. It goes after the parameter list (after `const` on a conversion operator)." },
    { TK::False, "literal", "", "The `bool` value false." },
    { TK::FallthroughKeyword, "keyword", "fallthrough;", "Ends a `switch` case to show that running into the next case is intended, which silences the warning." },
    { TK::ForKeyword, "keyword", "for (int: i = 0; i < 3; i++) { ... }", "Loops with an initializer, condition and increment (separated by `;` or `,`), or over arrays, strings, generators and iterable classes with `for (T: x in items)`." },
    { TK::FunctionKeyword, "keyword", "function name(int: x) -> int { ... } end function name", "Declares a function, closed with `end function name`. `-> T` gives the return type, `->` alone infers it, and no arrow returns a dynamic `var`; `function(int) -> int` is also a function type." },
    { TK::FriendKeyword, "keyword", "friend function peek(const Vault&: v) -> int;", "Gives a function, operator or class access to a class's private members. A friend defined inside the class isn't a member and belongs to the surrounding scope." },
    { TK::FromKeyword, "keyword", "import hello from \"lib.wal\"", "Names the file in an `import`, or the C/C++ files that hold the code in `extern \"C\" from \"file\"`. In a class heading it means the same as `inherits`." },
    { TK::GlobalKeyword, "keyword", "global int: count = 0;", "Visibility for a top-level or namespace-level declaration: visible in its file, to importers and to `extern` from other files. This is the default." },
    { TK::HoistKeyword, "keyword", "hoist int: later = 5;", "Lets a local variable be used before its declaration line." },
    { TK::HiddenKeyword, "keyword", "hidden int: count = 0;", "Visibility for a top-level or namespace-level declaration: only usable in its own file (or, inside a namespace, only within that namespace); it can't be imported or reached with `extern`." },
    { TK::IfKeyword, "keyword", "if (condition) then { ... }", "Runs a block when the condition is true; `then` and braces are required. The condition can declare a variable, and `if constexpr` chooses a branch at compile time." },
    { TK::ImportKeyword, "keyword", "import module.io", "Brings in names exported by another Walnut file, or a module found by name (library modules appear under `std::name`)." },
    { TK::IncludeKeyword, "keyword", "#include \"shapes.walh\"", "Used in the `#include` preprocessor directive, which inserts a `.wal` or `.walh` file. A header is pasted at most once into each file." },
    { TK::InKeyword, "keyword", "for (int: v in items) { ... }", "Separates the loop variable from the sequence in a `for ... in` loop over arrays, strings, generators, brace lists or classes with `begin()` and `end()`." },
    { TK::InlineKeyword, "keyword", "inline int: shared = 0;", "Modifier that allows a variable or function to be defined in several files." },
    { TK::InfinityKeyword, "literal", "", "Positive infinity, which every integer and float type can hold; `-inf` is negative infinity. It takes the type it goes into, and `x is inf` is true for both signs." },
    { TK::NanKeyword, "literal", "", "The special value for a result that exists but isn't a real number, such as `(-8.0) ** 0.5`. It is unordered, so test it with `x is nan`." },
    { TK::UndefinedKeyword, "literal", "", "The special value for a result that doesn't exist, such as `1 / 0`. Any number type can hold it, it is unordered, and `x is undefined` tests for it." },
    { TK::IndeterminateKeyword, "literal", "", "The special value for a form whose value can't be decided, such as `0 / 0` or `inf - inf`. It is unordered, so test it with `x is indeterminate`." },
    { TK::SpecialKeyword, "keyword", "special(string): name = lookup(-1);", "`special(T)` holds either a `T` or one of the special values (`inf`, `nan`, `undefined`, `indeterminate`), for types that can't hold them themselves. `T` can't be a number type, `var`, a reference, `void` or another `special`." },
    { TK::ImplicitKeyword, "keyword", "", "Reserved keyword; not described in the documentation. It can still be used as a function or method name." },
    { TK::InheritsKeyword, "keyword", "class Square inherits Shape { ... } end class Square", "Names the base classes in a class heading, separated by commas. All bases are public." },
    { TK::IsKeyword, "operator", "v is int", "Tests at run time whether a variant, `var` or polymorphic object currently holds or is a `T`. It also checks special values: `x is nan`, `is undefined`, `is indeterminate`, `is inf`." },
    { TK::LocalKeyword, "keyword", "local int: count = 0;", "Visibility for a top-level or namespace-level declaration: usable in its own file and reachable with `extern` from other files, but not imported." },
    { TK::MutableKeyword, "keyword", "mutable int: reads;", "Lets a class field change inside `const` methods. On a lambda, it lets the lambda change its own captured copies." },
    { TK::LazyKeyword, "keyword", "lazy float: area = compute_area();", "A class field computed from its initializer the first time it is read, then kept. `invalidate` discards the kept value so the next read recomputes it." },
    { TK::ModuleKeyword, "keyword", "module greet { ... }", "Groups declarations into a named module block that other files import with `import module \"greet\" from \"file.wal\"`. `import module.name` imports a module file found by name." },
    { TK::NamespaceKeyword, "keyword", "namespace math { ... } end namespace math", "Declares a namespace, closed with `end namespace name`; `namespace a::b` nests in one step and `namespace short = long::name;` makes an alias." },
    { TK::NewKeyword, "operator", "new int(7)", "Creates an object on the heap and returns a pointer; `new T[n]` creates an array. Free with `delete` or `delete[]`." },
    { TK::NoexceptKeyword, "keyword", "function quiet() noexcept -> int { ... } end function quiet", "Function qualifier saying the function doesn't throw; a `throw` written directly inside one is a compile-time error. `noexcept(expression)` is `true` when the expression can't throw." },
    { TK::NullptrKeyword, "literal", "", "The null pointer." },
    { TK::NodiscardKeyword, "keyword", "function checked(int: x) nodiscard(\"why\") -> int { ... } end function checked", "Function qualifier that makes ignoring the result a warning. The optional string gives the reason." },
    { TK::OperatorKeyword, "keyword", "operator(+) function(const Vec&: o) const -> Vec { ... }", "Defines an overloaded operator, a conversion operator (`operator(T)`), or a trait overload. Inside a class it is a member; at file or namespace level it is a free function." },
    { TK::OverrideKeyword, "keyword", "function area() const override -> int { ... } end function area", "Marks a method, after its parameter list, as overriding a base class's virtual method. Using it on a method that overrides nothing is an error." },
    { TK::PrivateKeyword, "keyword", "private:", "Starts a class section whose members only the class itself (and its friends) can use. Class members are private by default." },
    { TK::PublicKeyword, "keyword", "public:", "Starts a class section whose members anyone can use. Struct members are public by default; `public lazy` lets outside code assign and invalidate a lazy field." },
    { TK::ProtectedKeyword, "keyword", "protected:", "Starts a class section whose members the class and its derived classes can use." },
    { TK::RepeatKeyword, "keyword", "repeat;", "Runs the current loop iteration again from the top of the body, without checking the condition or running the `for` increment. `repeat label;` acts on a labelled loop." },
    { TK::ReinterpretCastKeyword, "operator", "reinterpret_cast<void*>(&storage[0])", "Converts pointer to pointer, or pointer to and from integer, reinterpreting the bits." },
    { TK::ReturnKeyword, "keyword", "return value;", "Leaves the function, giving its result. A `void` function needs no `return`, and returning a local variable moves it automatically." },
    { TK::RequiresKeyword, "keyword", "requires Number<T> && std::traits::is_integer<T>", "Adds a constraint to a template, written after `template <...>`. In a concept, `requires (params) { expressions; }` checks that the expressions compile." },
    { TK::StaticKeyword, "keyword", "static int: counter = 0;", "Gives a variable one copy for the whole program (a local keeps its value between calls). In a class, `static` fields and methods belong to the class and are reached as `Class::name`." },
    { TK::StaticCastKeyword, "operator", "static_cast<int>(3.99)", "The general explicit conversion: number to number (including narrowing and signed/unsigned), enum and integer, `char` to integer, `string` and `text`, base to derived class, and explicit conversion operators. It never turns a number into a `char` or `bool`." },
    { TK::StaticAssertKeyword, "operator", "static_assert(condition, \"message\");", "Stops compilation when the condition is false. The message is required." },
    { TK::SizeofKeyword, "operator", "sizeof(T)", "Gives the size of a type or value in bytes. Types without a fixed size, such as `string` and `var`, are an error; the method form `x.sizeof()` works on them." },
    { TK::StructKeyword, "keyword", "struct Point { ... } end struct Point", "Declares a struct, closed with `end struct Name`. It is like a class, but its members are public by default." },
    { TK::SwitchKeyword, "keyword", "switch (n) { case 1: ...; break; default: ... }", "Chooses a branch by value; works on integers, characters, enums, `string` and `text`. A case that runs into the next one gets a warning unless it ends with `fallthrough;`." },
    { TK::ThenKeyword, "keyword", "if (condition) then { ... }", "Required between an `if` or `else if` condition and its block, and after `else`." },
    { TK::ThisKeyword, "keyword", "this->value", "A pointer to the current object inside a method. `export this` exports every global declaration in the file." },
    { TK::SelfKeyword, "keyword", "self.x", "The current object itself, the same as `*this`; `self(x)` calls the object's own `operator(())`. It is `const` in a `const` method, and a lambda that captures `[this]` can use it." },
    { TK::ThrowKeyword, "keyword", "throw 42;", "Throws any value: a number, a string or an object. `throw;` on its own rethrows the exception currently being handled." },
    { TK::TryKeyword, "keyword", "try { ... } catch (int: e) { ... }", "Runs a block whose exceptions are handled by the `catch` clauses after it." },
    { TK::True, "literal", "", "The `bool` value true." },
    { TK::TypedefKeyword, "keyword", "typedef int: Meters;", "Declares another name for a type." },
    { TK::TypeofKeyword, "operator", "typeof(x): copy = x;", "Names the type of a value, without the reference, and is used wherever a type is written. Using it as a value is an error; use `typeid` for a type's name." },
    { TK::ThreadLocalKeyword, "keyword", "thread_local int: counter = 0;", "Modifier that gives each thread its own copy of a variable." },
    { TK::TemplateKeyword, "keyword", "template <typename T>", "Starts a template declaration for a function, class, variable, alias, constructor or operator. Parameters can be types, constants or packs; `template <>` writes a specialization." },
    { TK::TypenameKeyword, "keyword", "template <typename T>", "Declares a type parameter of a template; `typename... Ts` takes any number of types." },
    { TK::TypeidKeyword, "operator", "typeid(T)", "Gives a type's name as a `string`, such as `\"long int\"` or `\"Point\"`. For a `var` it is the type of the value it holds right now." },
    { TK::UnionKeyword, "keyword", "union Number { ... } end union Number", "Declares a union, whose members share the same storage so only one holds a value at a time. Members must be plain values, and unions have no generated `==` and can't inherit." },
    { TK::UsingKeyword, "keyword", "using Name = string;", "Declares a type alias, or with `using namespace n;` or `using a::b;` makes names usable without their prefix until the end of the block." },
    { TK::VariantKeyword, "keyword", "variant(int, string): v = 5;", "`variant(T1, T2, ...)` holds one value of one of the listed types at a time. Check it with `is` and read it with `as`." },
    { TK::VirtualKeyword, "keyword", "function describe() const virtual -> string { ... } end function describe", "Marks a method, after its parameter list, as able to be overridden by subclasses. A virtual method with no body that is never defined is abstract, with a warning." },
    { TK::AbstractKeyword, "keyword", "function area() const abstract -> int;", "Marks a method that has no body and must be implemented by subclasses; it implies `virtual`. A class with an unimplemented abstract method can't be created." },
    { TK::VoidKeyword, "type", "function increment(int&: value) -> void { ... } end function increment", "No value. It is only used as a return type, and a `void` function needs no `return`." },
    { TK::VolatileKeyword, "keyword", "volatile int: flag = 0;", "Modifier meaning every read and write really happens; the compiler doesn't optimize them away." },
    { TK::WhileKeyword, "keyword", "while (condition) { ... }", "Repeats a block or single statement while the condition is true. Also ends a `do ... while` loop." },
    { TK::AutoDeclaration, "type", "auto: total = count * 2;", "Takes its type from the initializer, which is required. An `auto:` parameter makes a generic lambda." },
    { TK::DynamicDeclaration, "type", "var x = 5;", "A dynamic value whose type can change at run time; it can hold a number, `bool`, `char`, string or function, and checks operations as the program runs. The colon is optional, and classes, pointers and arrays can't be stored in one." },
    { TK::BooleanDeclaration, "type", "bool: found = true;", "Holds `true` or `false`. Numbers never convert to `bool`; use a comparison such as `x != 0`." },
    { TK::CharacterDeclaration, "type", "char: c = 'd';", "One Unicode character (code point). A `char` plus or minus a whole number is another `char`, and `static_cast<int>(c)` gives its code point." },
    { TK::ConstantDeclaration, "keyword", "const int: c = 2;", "Modifier meaning the variable can't be assigned after initialization. After a method's parameter list, it means the method doesn't change the object." },
    { TK::DecimalDeclaration, "type", "float: ratio = 0.5;", "A fixed-width floating-point type. `float` is 128 bits, while `double` and `decimal` start two steps wider at 512 bits; `short` and `long` move the width one step each, and `unsigned` floats can't go below 0." },
    { TK::IntegerDeclaration, "type", "int: count = 3;", "A fixed-width signed integer. `int` is 32 bits and `integer` starts four steps wider at 512 bits; `short` and `long` move the width one step each. Overflow wraps around." },
    { TK::LongDeclaration, "type", "long int: n = 5;", "Makes a number type one step wider (up to two times): `long int` is 64 bits, `long long int` 128, `long float` 256. A bare `long` means `long int`." },
    { TK::StringDeclaration, "type", "string: name = \"walnut\";", "UTF-8 text written with `\"...\"` literals, which never interpolate. It doesn't convert implicitly to or from `text`; lengths and positions count characters, not bytes." },
    { TK::ShortDeclaration, "type", "short int: s = 3;", "Makes a number type one step narrower (up to two times): `short int` is 16 bits, `short short int` 8, `short float` 64. A bare `short` means `short int`." },
    { TK::UnsignedDeclaration, "type", "unsigned int: cap = inf;", "Makes any number type unsigned; it goes first, and a bare `unsigned` means `unsigned int`. Unsigned integers wrap around, and an unsigned float that would go negative becomes 0." },
    { TK::TextDeclaration, "type", "text: a = `n=${n}`;", "UTF-8 text written with backtick literals, which can interpolate `${expression}`. It works like `string` but is a separate type, so the two don't convert implicitly." },
};

const KeywordDoc* keyword_doc(TK kind) {
    for (const KeywordDoc& d : k_keyword_docs) if (d.kind == kind) return &d;
    return nullptr;
}

std::string keyword_aliases(TK kind, std::string_view written) {
    const tokenizing::Token t(kind, 0);
    std::string out;
    for (std::size_t i = 0; i < t.spelling_count(); ++i) {
        const std::string_view s = t.spellings()[i];
        if (s == written) continue;
        out += (out.empty() ? "" : ", ") + std::string("`") + std::string(s) + "`";
    }
    return out;
}

bool type_word(TK k) {
    switch (k) {
        case TK::UnsignedDeclaration: case TK::ShortDeclaration: case TK::LongDeclaration:
        case TK::IntegerDeclaration: case TK::DecimalDeclaration: case TK::CharacterDeclaration:
        case TK::BooleanDeclaration: case TK::StringDeclaration: case TK::TextDeclaration:
        case TK::AutoDeclaration: case TK::DynamicDeclaration: case TK::VoidKeyword:
            return true;
        default:
            return false;
    }
}

bool width_word(TK k) { return k == TK::ShortDeclaration || k == TK::LongDeclaration; }

std::string bits_text(double bits) {
    std::ostringstream o;
    o << static_cast<long long>(bits);
    return o.str();
}

std::string builtin_type_doc(const std::vector<std::string_view>& words) {
    bool is_unsigned = false;
    int steps = 0;
    std::string_view base;
    for (std::string_view w : words) {
        const TK k = get_keyword(w);
        if (k == TK::UnsignedDeclaration) is_unsigned = true;
        else if (k == TK::ShortDeclaration) --steps;
        else if (k == TK::LongDeclaration) ++steps;
        else base = w;
    }
    const TK bk = base.empty() ? TK::IntegerDeclaration : get_keyword(base);
    const auto scaled = [steps](double bits) { for (int i = 0; i < steps; ++i) bits *= 2; for (int i = 0; i > steps; --i) bits /= 2; return bits; };
    std::string what;
    if (bk == TK::IntegerDeclaration) {
        const double bits = scaled(base == "integer" ? 512.0 : 32.0);
        what = "A " + bits_text(bits) + "-bit " + (is_unsigned ? "unsigned" : "signed") + " integer.";
    } else if (bk == TK::DecimalDeclaration) {
        const double bits = scaled(base == "double" || base == "decimal" ? 512.0 : 128.0);
        what = "A " + bits_text(bits) + "-bit " + (is_unsigned ? "unsigned floating-point number, which can't hold a negative value." : "floating-point number.");
    } else if (const KeywordDoc* d = keyword_doc(bk); d && d->doc[0]) {
        what = d->doc;
    }
    return what;
}

class Collector {
public:
    Collector(const char* begin, std::size_t size, S::ConstEvaluator* eval = nullptr, S::TypeContext* types = nullptr) : m_begin(begin), m_end(begin + size), m_eval(eval), m_types(types) {
        m_line_starts.push_back(0);
        for (std::size_t i = 0; i < size; ++i) if (begin[i] == '\n') m_line_starts.push_back(i + 1);
    }

    Collector(const char* begin, std::size_t size, S::ConstEvaluator* eval, S::TypeContext* types, const SourceManager* sources, FileId main, S::Instantiator* inst) : Collector(begin, size, eval, types) {
        m_sources = sources;
        m_main = main;
        m_inst = inst;
    }

    bool inside(const char* p, std::size_t n = 1) const { return p && p >= m_begin && p + n <= m_end; }

    void walk_instantiation(N::ASTNode* n) {
        m_in_instantiation = true;
        walk(n);
        m_in_instantiation = false;
    }

    void walk(N::ASTNode* n) {
        if (!n || !m_seen.insert(n).second) return;

        switch (n->kind) {
            case K::Identifier: {
                auto* id = static_cast<N::Identifier*>(n);

                if (id->name != "self" && id->name != "this") {
                    use(id->name, id->resolved);
                    describe(id->name, id->resolved, n, 1);
                    overload_hover(id->name, id->resolved, nullptr, 1);
                } else {
                    expression_hover(id->name, n, 1);
                }
                break;
            }

            case K::QualifiedIdentifier: {
                auto* q = static_cast<N::QualifiedIdentifier*>(n);
                if (q->parts().empty()) break;
                use(q->parts().back(), q->resolved);
                describe(q->parts().back(), q->resolved, n, 1);
                qualifiers(q->parts(), q->parts().size() - 1, q->resolved);
                break;
            }

            case K::BinaryExpression: {
                auto* b = static_cast<N::BinaryExpression*>(n);
                if (b->resolved) op(b->at, b->op);
                expression_hover(op_spelling(b->at, b->op), n, 1);
                if (b->resolved) operator_hover(op_spelling(b->at, b->op), b->resolved, n);
                break;
            }

            case K::UnaryExpression: {
                auto* u = static_cast<N::UnaryExpression*>(n);
                if (u->resolved) op(u->at, u->op);
                expression_hover(op_spelling(u->at, u->op), n, 1);
                if (u->resolved) operator_hover(op_spelling(u->at, u->op), u->resolved, n);
                break;
            }

            case K::TernaryExpression:
                expression_hover(op_text_at(n->at, "?"), n, 1);
                break;

            case K::TypeQuery: {
                auto* t = static_cast<N::TypeQueryExpression*>(n);
                const std::string_view word = word_at(n->at);
                std::string denoted;

                if ((t->get_op() == N::TypeQueryExpression::Op::Typeof || t->get_op() == N::TypeQueryExpression::Op::Decltype) && t->get_operand()) {
                    const parser_types::TemplateArgument* a = t->get_operand();
                    if (a->is_type()) denoted = type_text(a->type, m_types);
                    else if (a->value) denoted = type_text(a->value->expr_type.type);
                }

                if (!denoted.empty()) hover(word, "", denoted, "", "", 1);
                else expression_hover(word, n, 1);
                if (t->get_operand() && t->get_operand()->is_type()) type(t->get_operand()->type);
                break;
            }

            case K::DereferenceExpression:
                if (auto* r = static_cast<N::DereferenceExpression*>(n)->resolved) { op_text(n->at, "*"); operator_hover(op_text_at(n->at, "*"), r, n); }
                break;

            case K::BitwiseNotExpression:
                if (auto* r = static_cast<N::BitwiseNotExpression*>(n)->resolved) { op_text(n->at, "~"); operator_hover(op_text_at(n->at, "~"), r, n); }
                break;

            case K::SubscriptExpression:
                if (auto* r = static_cast<N::SubscriptExpression*>(n)->resolved) { op_text(n->at, "["); operator_hover(op_text_at(n->at, "["), r, n); }
                break;

            case K::MemberAccessExpression: {
                auto* m = static_cast<N::MemberAccessExpression*>(n);
                const S::Symbol* member = m->resolved ? m->resolved : scope_member(m);
                if (!member) member = dot_member(m);
                const std::string_view member_text = member_name_in_source(m);
                use(member_text, member);
                describe(member_text, member, m->resolved ? n : nullptr, 1);
                if (!member && depends_on_template(m->m_object)) add(member_text, m_called.count(m) ? "method" : "property", 0, 0);
                if (m->m_op == N::MemberAccessExpression::Op::Arrow && !m->arrow_ops.empty()) op_text(n->at, "->");

                if (m->m_op == N::MemberAccessExpression::Op::Scope && m->m_object && m->m_object->kind == K::TemplateInstantiation) {
                    const N::ASTNode* t = static_cast<N::TemplateInstantiation*>(m->m_object)->m_template;
                    std::string_view name;
                    if (t && t->kind == K::Identifier) name = static_cast<const N::Identifier*>(t)->name;
                    else if (t && t->kind == K::QualifiedIdentifier) name = static_cast<const N::QualifiedIdentifier*>(t)->simple_name();
                    if (inside(name.data(), name.size())) m_template_qualifiers.insert(offset(name.data()));
                }
                break;
            }

            case K::CallExpression: {
                auto* c = static_cast<N::CallExpression*>(n);
                if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) m_called.insert(c->m_callee);
                if (c->resolved && c->m_callee && c->m_callee->kind == K::Identifier) {
                    auto* id = static_cast<N::Identifier*>(c->m_callee);
                    const bool constructor = follow(c->resolved) && follow(c->resolved)->decl && follow(c->resolved)->decl->kind == K::ConstructorDeclaration;
                    if (kind_of(c->resolved) != "operator" && !constructor) use(id->name, c->resolved, 2);
                }

                const std::string_view callee = callee_name(c->m_callee);

                if (!callee.empty()) {
                    const bool via_operator = c->resolved && kind_of(c->resolved) == "operator";
                    if (c->resolved && !via_operator) describe(callee, c->resolved, nullptr, 2);
                    hover(callee, "", type_text(n->expr_type.type), "", evaluate(n), 2);
                    if (c->resolved && !via_operator) overload_hover(callee, c->resolved, n, 2);
                    if (c->resolved && !via_operator) link(callee, c->resolved, 2);
                    if (!c->resolved) candidate_hover(callee, c);
                }

                targs(c->m_template_args);
                break;
            }

            case K::TemplateInstantiation: {
                auto* t = static_cast<N::TemplateInstantiation*>(n);
                targs(t->m_args);
                const std::string value = evaluate(n);
                if (!value.empty()) hover(callee_name(t->m_template), "", type_text(n->expr_type.type), "", value, 2);
                break;
            }

            case K::CastExpression:
                type(static_cast<N::CastExpression*>(n)->target);
                break;

            case K::NewExpression:
                type(static_cast<N::NewExpression*>(n)->type);
                break;

            case K::VariableDeclaration: {
                auto* v = static_cast<N::VariableDeclaration*>(n);
                type(v->m_type_info);
                declare(v->m_name, v->symbol, "variable");

                if (v->symbol) {
                    describe(v->m_name, v->symbol, nullptr, 3);
                    if (v->m_initializer && v->m_initializer->kind == K::BraceInitializerList) {
                        const auto* b = static_cast<const N::BraceInitializerList*>(v->m_initializer);
                        const S::Symbol* made = follow(b->ctor ? b->ctor : b->init_list_ctor);
                        if (made && made->kind == S::SymbolKind::Function) {
                            link(v->m_name, made, 4);
                            note(v->m_name, "", "Made with `" + function_signature(origin(made), m_types) + "`. Ctrl+click goes to that constructor.", 3);
                        }
                    }
                } else {
                    std::string t = type_text(v->m_type_info, m_types);
                    if ((t.empty() || t == "auto") && v->m_initializer) t = type_text(v->m_initializer->expr_type.type);
                    hover(v->m_name, t.empty() ? "" : t + ": " + std::string(v->m_name), "", "", "", 3);
                }
                break;
            }

            case K::ArrayDeclaration: {
                auto* a = static_cast<N::ArrayDeclaration*>(n);
                type(a->m_element_type);
                declare(a->m_name, a->symbol, "variable");
                if (a->symbol) describe(a->m_name, a->symbol, nullptr, 3);
                break;
            }

            case K::FunctionParameter: {
                auto* p = static_cast<N::FunctionParameter*>(n);
                type(p->m_type);
                if (p->is_iterable) type(p->iterable_element);
                declare(p->m_name, nullptr, "parameter");
                const std::string t = p->is_iterable ? iterable_text(p, m_types) : type_text(p->m_type, m_types);
                if (!t.empty()) hover(p->m_name, t + (p->is_variadic() ? "...: " : ": ") + std::string(p->m_name) + (p->m_initializer ? " = " + default_text(p->m_name, false) : std::string()), "", "", "", 3);
                break;
            }

            case K::FunctionDeclaration: {
                auto* f = static_cast<N::FunctionDeclaration*>(n);
                type(f->m_return_type);

                if (!f->m_name_parts.empty()) {
                    declare(f->m_name_parts.back(), f->symbol, f->variable_template ? "variable" : "function", f->variable_template ? VariableTemplate : 0);
                    std::string value;

                    if (!m_in_instantiation && m_eval && f->symbol && (!f->m_parameters || f->m_parameters->empty()) && f->m_modifiers.has(modifiers::RawModifiers::Constexpr)) {
                        value = value_text(m_eval->eval_value_function(f->symbol), f->m_return_type.canonical);
                    }

                    hover(f->m_name_parts.back(), template_prefix(f->symbol, m_types) + signature(f, m_types), "", "", value, 3);
                    overload_hover(f->m_name_parts.back(), f->symbol, nullptr, 3);
                }
                break;
            }

            case K::OperatorFunctionDeclaration: {
                auto* f = static_cast<N::OperatorFunctionDeclaration*>(n);
                type(f->m_return_type);
                type(f->conversion_type);

                if (f->symbol) {
                    const std::string_view word = word_at(n->at);
                    hover(word, function_signature(f->symbol, m_types), "", "", "", 3);
                    overload_hover(word, f->symbol, nullptr, 3);
                    link(word, f->symbol);
                }
                break;
            }

            case K::ConstructorDeclaration: {
                auto* f = static_cast<N::ConstructorDeclaration*>(n);

                if (f->symbol) {
                    const std::string_view word = word_at(n->at);
                    hover(word, function_signature(f->symbol, m_types), "", "", "", 3);
                    overload_hover(word, f->symbol, nullptr, 3);
                    link(word, f->symbol);
                }
                break;
            }

            case K::LambdaExpression: {
                auto* l = static_cast<N::LambdaExpression*>(n);
                type(l->m_return_type);
                for (N::LambdaExpression* s : l->specializations) walk(s);
                break;
            }

            case K::RecordDeclaration: {
                auto* r = static_cast<N::RecordDeclaration*>(n);
                if (!r->m_name_parts.empty()) {
                    declare(r->m_name_parts.back(), r->symbol, r->m_form == N::RecordDeclaration::Form::Class ? "class" : "struct");
                    describe(r->m_name_parts.back(), r->symbol, nullptr, 3);
                }
                type(r->m_inherits);
                for (const auto& b : r->m_extra_bases) type(b);
                targs(r->m_spec_args);
                break;
            }

            case K::EnumDeclaration: {
                auto* e = static_cast<N::EnumDeclaration*>(n);
                declare(e->name, e->symbol, "enum");
                describe(e->name, e->symbol, nullptr, 3);
                type(e->underlying_type);
                break;
            }

            case K::EnumValue: {
                auto* v = static_cast<N::EnumValue*>(n);
                declare(v->name, nullptr, "enumMember");
                if (v->symbol) describe(v->name, v->symbol, nullptr, 3);
                break;
            }

            case K::ImportExportDeclaration:
                imports(static_cast<N::ImportExportDeclaration*>(n));
                break;

            case K::NamespaceDeclaration:
                for (std::string_view part : static_cast<N::NamespaceDeclaration*>(n)->m_name_parts) declare(part, nullptr, "namespace");
                break;

            case K::TemplateParameter: {
                auto* t = static_cast<N::TemplateParameter*>(n);
                declare(t->m_name, nullptr, "typeParameter");
                hover(t->m_name, "(template parameter) " + template_parameter_text(t, m_types), "", "", "", 3);
                type(t->m_type);
                type(t->m_default_type);
                break;
            }

            case K::ForEachStatement: {
                auto* f = static_cast<N::ForEachStatement*>(n);
                type(f->m_element_type);
                declare(f->m_var_name, nullptr, "variable");
                if (const std::string t = type_text(f->m_element_type, m_types); !t.empty()) hover(f->m_var_name, t + ": " + std::string(f->m_var_name), "", "", "", 3);
                break;
            }

            case K::TryCatchStatement:
                for (auto* h = static_cast<N::TryCatchStatement*>(n); h; h = h->next_handler) {
                    type(h->catch_type);
                    declare(h->catch_name, nullptr, "variable");
                }
                break;

            case K::UsingDeclaration: {
                auto* u = static_cast<N::UsingDeclaration*>(n);
                if (u->variant == N::UsingDeclaration::Variant::Alias || u->variant == N::UsingDeclaration::Variant::Typedef) declare(u->name, nullptr, "type");
                if (u->variant == N::UsingDeclaration::Variant::NamespaceAlias) declare(u->name, nullptr, "namespace");
                type(u->aliased_type);
                break;
            }

            case K::ConceptDeclaration:
                declare(static_cast<N::ConceptDeclaration*>(n)->name, nullptr, "concept");
                break;

            case K::TemplateDeclaration: {
                auto* t = static_cast<N::TemplateDeclaration*>(n);
                for (N::TemplateParameter* p : t->m_params) walk(p);
                walk(t->m_requires_clause);
                break;
            }

            default:
                break;
        }

        N::for_each_child<true>(n, [this](N::ASTNode* c) { walk(c); });
    }

    std::string json(const ErrorReporter& errors, const WarningReporter& warnings, FileId main, const SourceManager& sources, bool semantic, std::size_t error_limit = static_cast<std::size_t>(-1), std::size_t warning_limit = static_cast<std::size_t>(-1)) const {
        std::ostringstream out;
        out << "{\"semantic\":" << (semantic ? "true" : "false") << ",\"tokens\":[";
        bool first = true;

        for (const auto& [at, e] : m_tokens) {
            std::uint32_t mods = e.mods;
            if (m_template_qualifiers.count(at)) mods |= TemplateQualifier;
            if (!first) out << ",";
            first = false;
            const Position p = position(at, e.len);
            out << "[" << p.line << "," << p.col << "," << p.len << "," << json_string(e.kind) << ",[";
            bool mfirst = true;

            for (std::size_t i = 0; i < sizeof(modifier_names) / sizeof(modifier_names[0]); ++i) {
                if (!(mods & (1u << i))) continue;
                if (!mfirst) out << ",";
                mfirst = false;
                out << json_string(modifier_names[i]);
            }

            out << "]]";
        }

        out << "],\"diagnostics\":[";
        first = true;

        const auto diagnostic = [&](const char* severity, FileId file, std::size_t line, const std::string& message, const char* at, std::size_t width) {
            if (!first) out << ",";
            first = false;
            std::string text = message;
            std::uint32_t l = line > 0 ? static_cast<std::uint32_t>(line - 1) : 0;
            std::uint32_t c = 0;
            std::uint32_t n = 0;

            if (inside(at)) {
                const Position p = position(offset(at), width ? width : 1);
                l = p.line; c = p.col; n = p.len;
            } else if (file != main && file != INVALID_FILE && file < sources.file_count()) {
                text = sources.absolute_path(file) + ":" + std::to_string(line) + ": " + message;
                l = 0;
            }

            out << "{\"severity\":\"" << severity << "\",\"line\":" << l << ",\"col\":" << c << ",\"len\":" << n << ",\"message\":" << json_string(text) << "}";
        };

        const auto& error_list = errors.errors();
        const auto& warning_list = warnings.warnings();
        const std::size_t error_count = std::min<std::size_t>(error_list.size(), error_limit);
        const std::size_t warning_count = std::min<std::size_t>(warning_list.size(), warning_limit);

        for (std::size_t i = 0; i < error_count; ++i) {
            const CompilerError& e = error_list[i];
            diagnostic("error", e.file_id, e.line, e.message, e.at, e.width);
        }

        for (std::size_t i = 0; i < warning_count; ++i) {
            const CompilerWarning& w = warning_list[i];
            diagnostic("warning", w.file_id, w.line, w.message, nullptr, 0);
        }
        out << "],\"hovers\":[";
        first = true;

        const auto list = [&out](const char* key, const std::vector<std::string>& items) {
            if (items.empty()) return;
            out << ",\"" << key << "\":[";

            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) out << ",";
                out << json_string(items[i]);
            }

            out << "]";
        };

        for (const auto& [at, h] : m_hovers) {
            if (h.code.empty() && h.types.empty() && h.values.empty() && h.results.empty() && h.doc.empty()) continue;
            if (!first) out << ",";
            first = false;
            const Position p = position(at, h.len);
            out << "{\"line\":" << p.line << ",\"col\":" << p.col << ",\"len\":" << p.len;
            if (!h.code.empty()) out << ",\"code\":" << json_string(h.code);
            if (h.overloads.size() > 1 || (h.guess && !h.overloads.empty())) list("overloads", h.overloads);
            if (h.guess) out << ",\"guess\":true";
            list("types", h.types);
            list("values", h.values);
            list("results", h.results);
            if (!h.doc.empty()) out << ",\"doc\":" << json_string(h.doc);
            out << "}";
        }

        out << "],\"locations\":[";
        first = true;

        for (const Location& l : m_locations) {
            if (!first) out << ",";
            first = false;
            out << "[" << json_string(m_paths[l.file]) << "," << l.line << "," << l.col << "," << l.len << "]";
        }

        out << "],\"links\":[";
        first = true;

        for (const auto& [at, k] : m_links) {
            if (!first) out << ",";
            first = false;
            const Position p = position(at, k.len);
            out << "[" << p.line << "," << p.col << "," << p.len << "," << k.definition << ",[";

            for (std::size_t i = 0; i < k.declarations.size(); ++i) {
                if (i) out << ",";
                out << k.declarations[i];
            }

            out << "]]";
        }

        out << "]}\n";
        return out.str();
    }

private:
    struct Entry {
        std::uint32_t    len  = 0;
        std::string_view kind;
        std::uint32_t    mods = 0;
        int              rank = 0;
    };

    struct Position {
        std::uint32_t line = 0;
        std::uint32_t col  = 0;
        std::uint32_t len  = 0;
    };

    const char*                          m_begin;
    const char*                          m_end;
    std::vector<std::size_t>             m_line_starts;
    std::map<std::size_t, Entry>         m_tokens;
    std::set<std::size_t>                m_template_qualifiers;
    std::unordered_set<const N::ASTNode*> m_seen;
    S::ConstEvaluator*                   m_eval = nullptr;
    S::TypeContext*                      m_types = nullptr;
    bool                                 m_in_instantiation = false;

    struct Hover {
        std::uint32_t            len  = 0;
        int                      rank = 0;
        int                      overload_rank = 0;
        std::vector<std::string> overloads;
        std::string              code;
        std::vector<std::string> types;
        std::vector<std::string> values;
        std::vector<std::string> results;
        bool                     guess = false;
        std::string              doc;
    };

    std::map<std::size_t, Hover> m_hovers;

    struct Location {
        std::size_t   file = 0;
        std::uint32_t line = 0;
        std::uint32_t col  = 0;
        std::uint32_t len  = 0;
    };

    struct Link {
        std::uint32_t            len = 0;
        int                      rank = 0;
        long                     definition = -1;
        std::vector<long>        declarations;
    };

    const SourceManager*                         m_sources = nullptr;
    FileId                                       m_main = INVALID_FILE;
    S::Instantiator*                             m_inst = nullptr;
    std::vector<std::string>                     m_paths;
    std::vector<Location>                        m_locations;
    std::map<const char*, long>                  m_location_ids;
    std::map<FileId, std::vector<std::size_t>>   m_file_lines;
    std::map<std::size_t, Link>                  m_links;
    const std::unordered_map<std::string, FileId>* m_imports = nullptr;
    std::vector<std::pair<std::string_view, const S::Symbol*>> m_declared;

    const S::Symbol* origin(const S::Symbol* s) const {
        s = follow(s);
        if (!s || !m_inst) return s;
        S::Instantiator::Instantiation* I = m_inst->owning(const_cast<S::Symbol*>(s));
        if (!I || I->sym != s || !I->source || !I->source->m_declaration) return s;
        const N::ASTNode* d = I->source->m_declaration;
        const S::Symbol* primary = nullptr;
        if (d->kind == K::FunctionDeclaration) primary = static_cast<const N::FunctionDeclaration*>(d)->symbol;
        else if (d->kind == K::OperatorFunctionDeclaration) primary = static_cast<const N::OperatorFunctionDeclaration*>(d)->symbol;
        else if (d->kind == K::ConstructorDeclaration) primary = static_cast<const N::ConstructorDeclaration*>(d)->symbol;
        return primary ? follow(primary) : s;
    }

    static bool is_callable(const S::Symbol* s) {
        if (!s || s->kind != S::SymbolKind::Function || !s->decl) return false;
        if (s->decl->kind == K::FunctionDeclaration) return !static_cast<const N::FunctionDeclaration*>(s->decl)->variable_template;
        return s->decl->kind == K::ConstructorDeclaration || s->decl->kind == K::OperatorFunctionDeclaration;
    }

    static bool same_operator(const S::Symbol* a, const S::Symbol* b) {
        if (!a || !b || !a->decl || !b->decl) return false;
        if (a->decl->kind != b->decl->kind) return false;
        if (a->decl->kind != K::OperatorFunctionDeclaration) return true;
        const auto* x = static_cast<const N::OperatorFunctionDeclaration*>(a->decl);
        const auto* y = static_cast<const N::OperatorFunctionDeclaration*>(b->decl);
        if (x->is_conversion() || y->is_conversion()) return x->is_conversion() && y->is_conversion();
        return x->get_overload() == y->get_overload();
    }

    std::vector<const S::Symbol*> overload_set(const S::Symbol* chosen, const N::ASTNode* site) const {
        std::vector<const S::Symbol*> out;
        const S::Symbol* first = origin(chosen);
        if (!is_callable(first)) return out;
        out.push_back(first);

        const auto add_one = [&out](const S::Symbol* s) {
            s = follow(s);
            if (s && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
        };

        if (site && m_types) {
            const auto it = m_types->overload_orders.find(site);
            if (it != m_types->overload_orders.end()) for (const S::Symbol* s : it->second) add_one(s);
        }

        if (first->owner) {
            for (const S::Symbol* o = first->owner->find_local(first->name); o; o = o->next_overload) {
                if (o->merged_into || !is_callable(o) || !same_operator(o, first)) continue;
                add_one(o);
            }
        }

        return out;
    }

    void overload_hover(std::string_view at, const S::Symbol* chosen, const N::ASTNode* site, int rank) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        const std::vector<const S::Symbol*> set = overload_set(chosen, site);
        if (set.size() < 2) return;
        Hover& h = m_hovers[offset(at.data())];
        if (h.len == 0) h.len = static_cast<std::uint32_t>(at.size());
        if (!h.overloads.empty() && (rank < h.overload_rank || h.guess)) return;
        std::vector<std::string> list;
        const S::Symbol* resolved = follow(chosen);
        std::string head = function_signature(resolved, m_types);
        if (head.empty()) head = function_signature(set.front(), m_types);
        list.push_back(head);

        for (std::size_t i = 1; i < set.size(); ++i) {
            const std::string sig = function_signature(set[i], m_types);
            if (!sig.empty() && std::find(list.begin(), list.end(), sig) == list.end()) list.push_back(sig);
        }

        if (list.size() < 2) return;
        h.overloads = std::move(list);
        h.overload_rank = rank;
        if (h.code.empty() || rank >= h.rank) { h.code = h.overloads.front(); h.rank = rank; }
    }

    static const N::FunctionParameters* parameters_of(const S::Symbol* s) {
        if (!s || !s->decl) return nullptr;
        if (s->decl->kind == K::FunctionDeclaration) return static_cast<const N::FunctionDeclaration*>(s->decl)->m_parameters;
        if (s->decl->kind == K::ConstructorDeclaration) return static_cast<const N::ConstructorDeclaration*>(s->decl)->m_parameters;
        if (s->decl->kind == K::OperatorFunctionDeclaration) return static_cast<const N::OperatorFunctionDeclaration*>(s->decl)->m_parameters;
        return nullptr;
    }

    static std::string bare_type(std::string t) {
        for (const char* word : { "const ", "volatile " }) {
            for (std::size_t at = t.find(word); at != std::string::npos; at = t.find(word)) t.erase(at, std::string(word).size());
        }
        while (!t.empty() && (t.back() == '&' || t.back() == ' ')) t.pop_back();
        return t;
    }

    static bool numeric_type(const std::string& t) {
        for (const char* n : { "bool", "char", "short", "int", "long", "float", "double", "unsigned", "signed", "size_t" }) {
            if (t.find(n) != std::string::npos && t.find('*') == std::string::npos && t.find('<') == std::string::npos) return true;
        }
        return false;
    }

    static int candidate_score(const S::Symbol* s, const N::CallExpression* c) {
        const N::FunctionParameters* ps = parameters_of(s);
        const std::size_t given = c->m_arguments.size();
        std::size_t required = 0;
        std::size_t total = 0;
        bool variadic = false;
        if (ps) {
            for (const N::FunctionParameter* p : ps->m_params) {
                if (!p) continue;
                if (p->is_variadic()) {
                    variadic = true;
                    continue;
                }
                ++total;
                if (!p->m_initializer) ++required;
            }
        }
        int score = 0;
        if (given >= required && (given <= total || variadic)) score += 1000;
        else score -= 50 * static_cast<int>(given < required ? required - given : given - total);
        if (!ps) return score;
        std::size_t i = 0;
        for (const N::FunctionParameter* p : ps->m_params) {
            if (!p || i >= given) break;
            const N::ASTNode* arg = c->m_arguments[i++];
            if (!arg || !arg->expr_type.type || arg->expr_type.type->is_dependent() || p->is_variadic()) continue;
            const std::string want = bare_type(type_text(p->m_type, nullptr));
            const std::string have = bare_type(type_text(arg->expr_type.type));
            if (want.empty() || have.empty()) continue;
            if (want == have) score += 20;
            else if (numeric_type(want) && numeric_type(have)) score += 8;
            else if ((want.find('*') != std::string::npos) == (have.find('*') != std::string::npos) && want.find_first_of("<") == std::string::npos) score += 2;
            else if (want.size() == 1 || (want.size() < 4 && std::isupper(static_cast<unsigned char>(want[0])))) score += 5;
            else score -= 10;
        }
        return score;
    }

    std::vector<const S::Symbol*> candidates_for(const N::CallExpression* c) const {
        std::vector<const S::Symbol*> out;
        const auto add_chain = [&out](const S::Symbol* first) {
            for (const S::Symbol* o = first; o; o = o->next_overload) {
                const S::Symbol* f = follow(o);
                if (!f || o->merged_into || !is_callable(f)) continue;
                if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
            }
        };
        const auto add_constructors = [&](const S::Symbol* type) {
            type = follow(type);
            if (!type || type->kind != S::SymbolKind::Type || !type->inner_scope) return;
            add_chain(type->inner_scope->find_local("constructor"));
        };
        const N::ASTNode* callee = c->m_callee;
        if (callee && callee->kind == K::TemplateInstantiation) callee = static_cast<const N::TemplateInstantiation*>(callee)->m_template;
        const S::Symbol* named = nullptr;
        if (callee && callee->kind == K::Identifier) named = static_cast<const N::Identifier*>(callee)->resolved;
        else if (callee && callee->kind == K::QualifiedIdentifier) named = static_cast<const N::QualifiedIdentifier*>(callee)->resolved;
        else if (callee && callee->kind == K::MemberAccessExpression) {
            const auto* m = static_cast<const N::MemberAccessExpression*>(callee);
            named = m->resolved ? m->resolved : scope_member(m);
            if (!named) named = dot_member(m);
        }
        named = follow(named);
        if (!named) return out;
        if (named->kind == S::SymbolKind::Type) {
            add_constructors(named);
            return out;
        }
        if (!is_callable(named)) return out;
        if (named->owner) add_chain(named->owner->find_local(named->name));
        if (out.empty()) out.push_back(named);
        return out;
    }

    void candidate_hover(std::string_view at, const N::CallExpression* c) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        std::vector<const S::Symbol*> set = candidates_for(c);
        if (set.empty()) return;
        std::vector<std::pair<int, const S::Symbol*>> ranked;
        for (const S::Symbol* s : set) ranked.emplace_back(candidate_score(s, c), s);
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<std::string> list;
        for (const auto& r : ranked) {
            const std::string sig = function_signature(r.second, m_types);
            if (!sig.empty() && std::find(list.begin(), list.end(), sig) == list.end()) list.push_back(sig);
        }
        if (list.empty()) return;
        Hover& h = m_hovers[offset(at.data())];
        if (h.len == 0) h.len = static_cast<std::uint32_t>(at.size());
        h.overloads = list;
        h.overload_rank = 3;
        h.code = list.front();
        h.rank = 3;
        h.guess = true;
    }

    void operator_hover(std::string_view at, const S::Symbol* chosen, const N::ASTNode* site) {
        if (at.empty() || !chosen) return;
        const std::string sig = function_signature(chosen, m_types);
        if (!sig.empty()) hover(at, sig, "", "", "", 2);
        overload_hover(at, chosen, site, 2);
        link(at, chosen, 2);
    }

    FileId file_holding(const char* p) const {
        if (!m_sources || !p) return INVALID_FILE;
        if (m_main != INVALID_FILE && inside(p)) return m_main;

        for (std::size_t i = 0; i < m_sources->file_count(); ++i) {
            const FileId id = static_cast<FileId>(i);
            if (id == m_main) continue;
            const char* b = m_sources->data(id);
            if (b && p >= b && p < b + m_sources->size(id)) return id;
        }

        return INVALID_FILE;
    }

    long location_of(std::string_view name) {
        if (name.empty()) return -1;
        const auto known = m_location_ids.find(name.data());
        if (known != m_location_ids.end()) return known->second;
        const FileId file = file_holding(name.data());
        if (file == INVALID_FILE) return -1;
        Location l;

        if (file == m_main) {
            const Position p = position(offset(name.data()), name.size());
            l.line = p.line; l.col = p.col; l.len = p.len;
        } else {
            const char* b = m_sources->data(file);
            auto& lines = m_file_lines[file];

            if (lines.empty()) {
                lines.push_back(0);
                for (std::size_t i = 0; i < m_sources->size(file); ++i) if (b[i] == '\n') lines.push_back(i + 1);
            }

            const std::size_t at = static_cast<std::size_t>(name.data() - b);
            const auto it = std::upper_bound(lines.begin(), lines.end(), at);
            const std::size_t line = static_cast<std::size_t>(it - lines.begin()) - 1;
            l.line = static_cast<std::uint32_t>(line);
            l.col  = utf16_units(b + lines[line], at - lines[line]);
            l.len  = utf16_units(name.data(), name.size());
        }

        const std::string path = file == m_main ? std::string() : m_sources->absolute_path(file);
        auto p = std::find(m_paths.begin(), m_paths.end(), path);
        if (p == m_paths.end()) { m_paths.push_back(path); p = m_paths.end() - 1; }
        l.file = static_cast<std::size_t>(p - m_paths.begin());
        const long id = static_cast<long>(m_locations.size());
        m_locations.push_back(l);
        m_location_ids.emplace(name.data(), id);
        return id;
    }

    std::string_view name_in(const N::ASTNode* d) const {
        std::string_view name = decl_name(d);
        if (name.empty() && d && (d->kind == K::ConstructorDeclaration || d->kind == K::OperatorFunctionDeclaration)) {
            const char* p = d->at;
            if (p) {
                const char* e = p;
                while (word_start(*e) || (*e >= '0' && *e <= '9')) ++e;
                name = std::string_view(p, static_cast<std::size_t>(e - p));
            }
        }
        return name;
    }

    void link(std::string_view at, const S::Symbol* original, int rank = 1) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        const auto existing = m_links.find(offset(at.data()));
        if (existing != m_links.end() && existing->second.rank > rank) return;
        const S::Symbol* s = follow(original);
        if (!s) return;
        const S::Symbol* target = s->extern_target ? follow(s->extern_target) : s;
        if (!target) target = s;
        const long def = location_of(name_in(target->decl));
        if (def < 0) return;
        Link k;
        k.len = static_cast<std::uint32_t>(at.size());
        k.rank = rank;
        k.definition = def;

        const auto add_decl = [&k, this](const N::ASTNode* d) {
            const long id = location_of(name_in(d));
            if (id >= 0 && id != k.definition && std::find(k.declarations.begin(), k.declarations.end(), id) == k.declarations.end()) k.declarations.push_back(id);
        };

        for (const N::ASTNode* d : target->declarations) add_decl(d);
        if (target != s) { add_decl(s->decl); for (const N::ASTNode* d : s->declarations) add_decl(d); }
        m_links[offset(at.data())] = std::move(k);
    }

    static constexpr std::size_t max_alternatives = 6;

    static void remember(std::vector<std::string>& list, const std::string& text) {
        if (text.empty() || list.size() >= max_alternatives) return;
        if (std::find(list.begin(), list.end(), text) == list.end()) list.push_back(text);
    }

    void note(std::string_view at, const std::string& code, const std::string& doc, int rank) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        Hover& h = m_hovers[offset(at.data())];
        if (h.len == 0) h.len = static_cast<std::uint32_t>(at.size());
        if (!code.empty() && (h.code.empty() || rank > h.rank)) {
            h.code = code;
            h.rank = rank;
        }
        if (!doc.empty() && h.doc.empty()) h.doc = doc;
    }

    bool has_hover(const char* at) const {
        const auto it = m_hovers.find(offset(at));
        return it != m_hovers.end() && (!it->second.code.empty() || !it->second.doc.empty());
    }

    std::string library_file(const S::Symbol* s) {
        if (!s || !m_sources) return std::string();
        const std::string_view name = name_in(s->decl);
        if (name.empty()) return std::string();
        const FileId file = file_holding(name.data());
        if (file == INVALID_FILE) return std::string();
        const std::string& path = m_sources->absolute_path(file);
        const std::string root = std::string(SourceManager::LIBRARY_ROOT) + "/";
        return path.rfind(root, 0) == 0 ? path.substr(root.size()) : std::string();
    }

public:
    void set_imports(const std::unordered_map<std::string, FileId>* imports) { m_imports = imports; }

    long path_location(const std::string& path) {
        auto p = std::find(m_paths.begin(), m_paths.end(), path);
        if (p == m_paths.end()) { m_paths.push_back(path); p = m_paths.end() - 1; }
        Location l;
        l.file = static_cast<std::size_t>(p - m_paths.begin());
        const long id = static_cast<long>(m_locations.size());
        m_locations.push_back(l);
        return id;
    }

    std::string include_target(std::string_view written, bool angled) const {
        if (!m_sources || m_main == INVALID_FILE || written.empty()) return std::string();
        namespace fs = std::filesystem;
        std::error_code ec;
        const std::string want(written);
        if (!angled) {
            const std::string& main_path = m_sources->absolute_path(m_main);
            const fs::path here = fs::path(main_path).parent_path() / want;
            if (main_path.rfind(SourceManager::LIBRARY_ROOT, 0) != 0 && fs::is_regular_file(here, ec)) return fs::absolute(here, ec).lexically_normal().string();
        }
        for (std::size_t i = 0; i < m_sources->file_count(); ++i) {
            const std::string& path = m_sources->absolute_path(static_cast<FileId>(i));
            const std::string generic = fs::path(path).generic_string();
            if (generic.size() > want.size() && generic.compare(generic.size() - want.size(), want.size(), want) == 0 && generic[generic.size() - want.size() - 1] == '/') return path;
        }
        return std::string();
    }

    void include_link(const char* line_start, const char* line_end) {
        const char* p = line_start;
        while (p < line_end && (*p == ' ' || *p == '\t')) ++p;
        if (p >= line_end || *p != '#') return;
        ++p;
        while (p < line_end && (*p == ' ' || *p == '\t')) ++p;
        if (line_end - p < 7 || std::string_view(p, 7) != "include") return;
        p += 7;
        while (p < line_end && (*p == ' ' || *p == '\t')) ++p;
        if (p >= line_end || (*p != '"' && *p != '<')) return;
        const char close = *p == '"' ? '"' : '>';
        const bool angled = *p == '<';
        const char* b = ++p;
        while (p < line_end && *p != close) ++p;
        if (p >= line_end || p == b) return;
        const std::string_view written(b, static_cast<std::size_t>(p - b));
        const std::string target = include_target(written, angled);
        if (target.empty()) return;
        const std::string root = std::string(SourceManager::LIBRARY_ROOT) + "/";
        const std::string shown = target.rfind(root, 0) == 0 ? target.substr(root.size()) : target;
        note(written, "#include " + std::string(1, angled ? '<' : '"') + std::string(written) + (angled ? ">" : "\""), "**Included file** `" + shown + "`. Ctrl+click opens it.", 3);
        Link k;
        k.len = static_cast<std::uint32_t>(written.size());
        k.rank = 3;
        k.definition = path_location(target == m_sources->absolute_path(m_main) ? std::string() : target);
        m_links[offset(written.data())] = std::move(k);
    }

    bool linked(const char* at) const { return m_links.count(offset(at)) != 0; }

    bool coded(const char* at) const {
        const auto it = m_hovers.find(offset(at));
        return it != m_hovers.end() && !it->second.code.empty();
    }

    void words() {
        const char* p = m_begin;
        const char* line_start = m_begin;
        bool directive = false;
        struct Word { const char* at; std::string_view text; TK kind; };
        std::vector<Word> list;
        const auto line_end = [this](const char* q) { while (q < m_end && *q != '\n') ++q; return q; };
        include_link(line_start, line_end(line_start));
        {
            const char* q = m_begin;
            while (q < m_end && (*q == ' ' || *q == '\t')) ++q;
            directive = q < m_end && *q == '#';
        }
        while (p < m_end) {
            const char c = *p;
            if (c == '\n') {
                ++p;
                line_start = p;
                include_link(line_start, line_end(line_start));
                const char* q = p;
                while (q < m_end && (*q == ' ' || *q == '\t')) ++q;
                directive = q < m_end && *q == '#';
                continue;
            }
            if (directive) { ++p; continue; }
            if (c == '/' && p + 1 < m_end && p[1] == '/') { while (p < m_end && *p != '\n') ++p; continue; }
            if (c == '/' && p + 1 < m_end && p[1] == '*') {
                int depth = 0;
                while (p < m_end) {
                    if (p + 1 < m_end && p[0] == '/' && p[1] == '*') { ++depth; p += 2; continue; }
                    if (p + 1 < m_end && p[0] == '*' && p[1] == '/') { p += 2; if (--depth == 0) break; continue; }
                    ++p;
                }
                continue;
            }
            if (c == '"' || c == '\'' || c == '`') {
                ++p;
                while (p < m_end && *p != c) { if (*p == '\\') ++p; ++p; }
                ++p;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') { ++p; continue; }
            if (word_start(c)) {
                const char* b = p;
                while (p < m_end && (word_start(*p) || (*p >= '0' && *p <= '9'))) ++p;
                const std::string_view w(b, static_cast<std::size_t>(p - b));
                const TK k = get_keyword(w);
                if (k != TK::Unexpected) list.push_back(Word{ b, w, k });
                else list.push_back(Word{ b, w, TK::Identifier });
                continue;
            }
            if (c >= '0' && c <= '9') { while (p < m_end && (word_start(*p) || (*p >= '0' && *p <= '9') || *p == '.')) ++p; continue; }
            list.push_back(Word{ p, std::string_view(p, 1), TK::Unexpected });
            ++p;
        }

        const auto after_member = [&list](std::size_t i) {
            if (i == 0) return false;
            const std::string_view prev = list[i - 1].text;
            if (prev == ".") return true;
            if (i >= 2 && ((prev == ">" && list[i - 2].text == "-") || (prev == ":" && list[i - 2].text == ":"))) return true;
            return false;
        };

        for (std::size_t i = 0; i + 2 < list.size(); ++i) {
            if (list[i].kind != TK::EndKeyword || list[i + 1].kind == TK::Unexpected || list[i + 1].kind == TK::Identifier) continue;
            const Word& name = list[i + 2];
            if (name.kind == TK::Unexpected || coded(name.at)) continue;
            const S::Symbol* found = nullptr;
            for (const auto& [text, sym] : m_declared) {
                if (text == name.text && text.data() < name.at) found = sym;
            }
            if (!found) continue;
            link(name.text, found);
            describe(name.text, found, nullptr, 1);
        }

        for (std::size_t i = 0; i < list.size(); ++i) {
            const Word& w = list[i];
            if (w.kind == TK::Unexpected || w.kind == TK::Identifier) continue;
            if (after_member(i) || coded(w.at) || linked(w.at)) continue;
            if (type_word(w.kind) && w.kind != TK::VoidKeyword && w.kind != TK::AutoDeclaration && w.kind != TK::DynamicDeclaration) {
                std::vector<std::string_view> run{ w.text };
                std::size_t j = i + 1;
                bool has_base = !width_word(w.kind) && w.kind != TK::UnsignedDeclaration;
                const bool numeric_run = w.kind == TK::UnsignedDeclaration || width_word(w.kind) || w.kind == TK::IntegerDeclaration || w.kind == TK::DecimalDeclaration;
                while (numeric_run && !has_base && j < list.size() && (width_word(list[j].kind) || list[j].kind == TK::IntegerDeclaration || list[j].kind == TK::DecimalDeclaration)) {
                    run.push_back(list[j].text);
                    has_base = !width_word(list[j].kind);
                    ++j;
                }
                const char* end = run.back().data() + run.back().size();
                const std::string_view at(w.at, static_cast<std::size_t>(end - w.at));
                std::string code;
                for (std::string_view part : run) code += (code.empty() ? "" : " ") + std::string(part);
                std::string doc = "**Walnut builtin type.**";
                const std::string what = builtin_type_doc(run);
                if (!what.empty()) doc += " " + what;
                if (run.size() == 1) {
                    const std::string aliases = keyword_aliases(w.kind, w.text);
                    if (!aliases.empty() && !numeric_run) doc += " Also written " + aliases + ".";
                }
                note(at, code, doc, 0);
                i = j - 1;
                continue;
            }
            const KeywordDoc* d = keyword_doc(w.kind);
            const std::string category = d ? d->category : "keyword";
            std::string doc = category == "type" ? "**Walnut builtin type.**" : category == "literal" ? "**Walnut literal.**" : category == "operator" ? "**Walnut builtin operator.**" : "**Walnut keyword.**";
            if (d && d->doc[0]) doc += std::string(" ") + d->doc;
            const std::string aliases = keyword_aliases(w.kind, w.text);
            if (!aliases.empty()) doc += " Also written " + aliases + ".";
            note(w.text, d && d->syntax[0] ? std::string(d->syntax) : std::string(w.text), doc, 0);
        }
    }

    long file_location(FileId file) {
        if (!m_sources || file == INVALID_FILE) return -1;
        const std::string path = file == m_main ? std::string() : m_sources->absolute_path(file);
        auto p = std::find(m_paths.begin(), m_paths.end(), path);
        if (p == m_paths.end()) { m_paths.push_back(path); p = m_paths.end() - 1; }
        Location l;
        l.file = static_cast<std::size_t>(p - m_paths.begin());
        const long id = static_cast<long>(m_locations.size());
        m_locations.push_back(l);
        return id;
    }

    void file_link(std::string_view at, FileId file) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        const long def = file_location(file);
        if (def < 0) return;
        Link k;
        k.len = static_cast<std::uint32_t>(at.size());
        k.rank = 3;
        k.definition = def;
        m_links[offset(at.data())] = std::move(k);
    }

    std::string shown_file(FileId file) const {
        const std::string& path = m_sources->absolute_path(file);
        const std::string root = std::string(SourceManager::LIBRARY_ROOT) + "/";
        return path.rfind(root, 0) == 0 ? path.substr(root.size()) : path;
    }

    bool library(FileId file) const {
        return m_sources && m_sources->absolute_path(file).rfind(std::string(SourceManager::LIBRARY_ROOT) + "/", 0) == 0;
    }

    void imports(const N::ImportExportDeclaration* d) {
        if (!d->is_import() || !m_imports || !m_sources) return;
        for (const N::ImportExportItem& it : d->get_items()) {
            if (!it.has_source) continue;
            const auto found = m_imports->find(std::string(it.source));
            if (found == m_imports->end()) continue;
            const FileId file = found->second;
            std::string_view at;
            if (it.from_module_path && !it.module_parts.empty()) {
                const char* b = it.module_parts.front().data();
                const char* e = it.module_parts.back().data() + it.module_parts.back().size();
                if (!inside(b) || !inside(e - 1) || e <= b) continue;
                if (inside(b - 7, 7) && std::string_view(b - 7, 7) == "module.") b -= 7;
                at = std::string_view(b, static_cast<std::size_t>(e - b));
            } else {
                at = it.source;
                if (!inside(at.data(), at.size())) continue;
                if (at.size() >= 2 && at.front() == '"' && at.back() == '"') at = at.substr(1, at.size() - 2);
            }
            const std::string shown = shown_file(file);
            if (it.from_module_path) {
                std::string dotted;
                for (std::string_view part : it.module_parts) dotted += (dotted.empty() ? "" : ".") + std::string(part);
                if (library(file)) note(at, "module std::" + dotted, "**Walnut library module**, from `" + shown + "`. Ctrl+click opens it.", 3);
                else note(at, "module " + dotted, "**Module**, from `" + shown + "`. Ctrl+click opens it.", 3);
            } else {
                note(at, "file \"" + std::string(at) + "\"", "**Imported file** `" + shown + "`. Ctrl+click opens it.", 3);
            }
            file_link(at, file);
        }
    }

    std::string enum_underlying(const S::Symbol* e) const {
        if (!e || !e->decl || e->decl->kind != K::EnumDeclaration) return "int";
        const auto* d = static_cast<const N::EnumDeclaration*>(e->decl);
        if (!d->has_underlying_type) return "int";
        const std::string t = type_text(d->underlying_type, m_types);
        return t.empty() ? std::string("int") : t;
    }

    void hover(std::string_view at, const std::string& code, const std::string& type, const std::string& value, const std::string& result, int rank) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        if (code.empty() && type.empty() && value.empty() && result.empty()) return;
        Hover& h = m_hovers[offset(at.data())];
        if (h.len == 0) h.len = static_cast<std::uint32_t>(at.size());

        if (!code.empty() && (h.code.empty() || rank > h.rank)) {
            h.code = code;
            h.rank = rank;
        }

        remember(h.types, type);
        remember(h.values, value);
        remember(h.results, result);
    }

    std::string evaluate(N::ASTNode* e) {
        if (!m_eval || !e) return std::string();
        S::Type* t = e->expr_type.type;
        if (t && t->is_dependent()) return std::string();
        const S::ConstValue v = m_eval->eval(e);
        if (!v.ok()) return std::string();
        return value_text(v, t);
    }

    void expression_hover(std::string_view at, N::ASTNode* e, int rank) {
        if (at.empty() || !e) return;
        hover(at, "", type_text(e->expr_type.type), evaluate(e), "", rank);
    }

    static std::string qualified_name(const S::Symbol* s) {
        if (!s) return std::string();
        std::vector<std::string_view> parts{ s->name };
        const S::Scope* sc = s->owner;

        for (int guard = 0; sc && guard < 32; ++guard, sc = sc->parent) {
            const S::Symbol* o = sc->owner_symbol;
            if (!o || o == s || o->name.empty()) continue;
            if (o->kind != S::SymbolKind::Namespace && o->kind != S::SymbolKind::Module && o->kind != S::SymbolKind::Type && o->kind != S::SymbolKind::Enum) continue;
            parts.insert(parts.begin(), o->name);
        }

        return joined(parts);
    }

    std::string symbol_type(const S::Symbol* s, const N::ASTNode* use) const {
        std::string t;
        if (s->type) t = type_text(*s->type, m_types);

        if (t.empty() || t == "auto" || t.find("typeof(") != std::string::npos || t.find("decltype(") != std::string::npos) {
            if (use && use->expr_type.type) t = type_text(use->expr_type.type);
            else if (s->decl && s->decl->kind == K::VariableDeclaration) {
                const auto* v = static_cast<const N::VariableDeclaration*>(s->decl);
                if (v->m_initializer && v->m_initializer->expr_type.type) t = type_text(v->m_initializer->expr_type.type);
            }
        }

        return t;
    }

    void describe(std::string_view at, const S::Symbol* original, N::ASTNode* use, int rank) {
        const S::Symbol* s = follow(original);
        if (!s || at.empty()) return;
        std::string code;
        std::string type;
        std::string value;

        switch (s->kind) {
            case S::SymbolKind::Variable: {
                std::string mods;
                if (s->decl && s->decl->kind == K::VariableDeclaration) mods = prefix(static_cast<const N::VariableDeclaration*>(s->decl)->get_type_info().modifiers);
                const std::string t = symbol_type(s, use);
                const std::string shown = mods.find("constexpr") != std::string::npos ? without_const(t) : t;
                code = mods + (shown.empty() ? std::string() : shown + ": ") + qualified_name(s);
                if (s->decl && s->decl->kind == K::VariableDeclaration) {
                    const auto* v = static_cast<const N::VariableDeclaration*>(s->decl);
                    if (v->m_initializer) {
                        const std::string init = default_after(v->m_name, false);
                        if (!init.empty()) code += " = " + init;
                    }
                }
                if (m_eval) {
                    m_eval->hidden_random_hit = false;
                    value = value_text(m_eval->value_of(const_cast<S::Symbol*>(s)), s->type ? s->type->canonical : nullptr);
                    if (m_eval->hidden_random_hit) note(at, "", S::compile_time_random_is_seeded() ? "**Picked while compiling** by `std::random` from the `--random-seed` you gave, so it's the same every build." : "**Picked while compiling** by `std::random` from the operating system's secure random source, so each build gets its own value.", rank);
                }
                break;
            }

            case S::SymbolKind::Parameter: {
                const std::string t = symbol_type(s, use);
                code = (t.empty() ? std::string() : t + ": ") + std::string(s->name);
                if (s->decl && s->decl->kind == K::FunctionParameter) {
                    const auto* p = static_cast<const N::FunctionParameter*>(s->decl);
                    if (p->m_initializer) code += " = " + default_text(p->m_name, false);
                }
                break;
            }

            case S::SymbolKind::Function:
                code = function_signature(s, m_types);
                break;

            case S::SymbolKind::Type: {
                std::string head = template_prefix(s, m_types);
                if (!head.empty()) head.back() = '\n';
                if (s->decl && s->decl->kind == K::RecordDeclaration && static_cast<const N::RecordDeclaration*>(s->decl)->m_form != N::RecordDeclaration::Form::Class) code = head + "struct " + qualified_name(s);
                else code = head + "class " + qualified_name(s);
                break;
            }

            case S::SymbolKind::TypeAlias: {
                code = "using " + qualified_name(s);
                const std::string t = s->type ? type_text(*s->type, m_types) : std::string();
                if (!t.empty()) code += " = " + t;
                break;
            }

            case S::SymbolKind::Enum:          code = "enum " + qualified_name(s) + " : " + enum_underlying(s); break;
            case S::SymbolKind::EnumConstant: {
                const S::Symbol* owner = s->owner ? follow(s->owner->owner_symbol) : nullptr;
                code = enum_underlying(owner) + ": " + qualified_name(s);
                if (m_eval) value = value_text(m_eval->value_of(const_cast<S::Symbol*>(s)), nullptr);
                if (!value.empty()) code += " = " + value;
                break;
            }
            case S::SymbolKind::Namespace:
            case S::SymbolKind::Module: {
                const std::string name = qualified_name(s);
                code = "namespace " + name;
                const std::string file = library_file(s);
                if (name == "std") note(at, "", "**Walnut library.** Every library module lives in `std`: `import module.io` gives `std::io`, and `std::core` is always there.", rank);
                else if (!file.empty() && name.rfind("std::", 0) == 0 && name.find("::", 5) == std::string::npos) {
                    const std::string module = name.substr(5);
                    note(at, "", module == "core" ? "**Walnut library module** `core`, from `core.walh`. It's always available, so it needs no import." : "**Walnut library module** `" + module + "`, from `" + file + "`. `import module." + module + "` brings it in.", rank);
                }
                break;
            }
            case S::SymbolKind::Concept:       code = "concept " + qualified_name(s); break;
            case S::SymbolKind::TemplateParam:
                if (s->decl && s->decl->kind == K::TemplateParameter) code = "(template parameter) " + template_parameter_text(static_cast<const N::TemplateParameter*>(s->decl), m_types);
                else code = "template parameter " + std::string(s->name);
                break;
            case S::SymbolKind::Import:        break;
        }

        if (use && (s->kind == S::SymbolKind::Variable || s->kind == S::SymbolKind::Parameter || s->kind == S::SymbolKind::TemplateParam)) {
            if (value.empty()) value = evaluate(use);
            if (s->kind == S::SymbolKind::TemplateParam || m_in_instantiation) type = type_text(use->expr_type.type);
        }

        hover(at, code, type, value, "", rank);
    }

    std::unordered_set<const N::ASTNode*> m_called;

    static bool depends_on_template(const N::ASTNode* o) {
        if (!o) return false;
        const S::Type* t = o->expr_type.type;
        return !t || t->is_dependent();
    }

    static const S::Symbol* dot_member(const N::MemberAccessExpression* m) {
        if (m->m_op == N::MemberAccessExpression::Op::Scope || !m->m_object || m->m_member.empty()) return nullptr;
        const N::ASTNode* o = m->m_object;
        const S::Symbol* object = nullptr;
        if (o->kind == K::Identifier) object = static_cast<const N::Identifier*>(o)->resolved;
        else if (o->kind == K::MemberAccessExpression) object = static_cast<const N::MemberAccessExpression*>(o)->resolved;
        object = follow(object);
        if (!object || (object->kind != S::SymbolKind::Variable && object->kind != S::SymbolKind::Parameter) || !object->type) return nullptr;
        const S::Symbol* record = follow(object->type->resolved);
        if (!record || record->kind != S::SymbolKind::Type || !record->inner_scope) return nullptr;
        return record->inner_scope->find_member(m->m_member);
    }

    std::string_view member_name_in_source(const N::MemberAccessExpression* m) const {
        if (m->m_member.empty() || inside(m->m_member.data(), m->m_member.size())) return m->m_member;
        const char* p = m->at;
        if (!inside(p)) return m->m_member;
        if (*p == '.') ++p;
        else if (inside(p, 2) && (std::string_view(p, 2) == "->" || std::string_view(p, 2) == "::")) p += 2;
        else return m->m_member;
        while (inside(p) && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
        if (!inside(p, m->m_member.size()) || std::string_view(p, m->m_member.size()) != m->m_member) return m->m_member;
        return std::string_view(p, m->m_member.size());
    }

    static const S::Symbol* scope_member(const N::MemberAccessExpression* m) {
        if (m->m_op != N::MemberAccessExpression::Op::Scope || !m->m_object || m->m_member.empty()) return nullptr;
        const N::ASTNode* o = m->m_object;
        if (o->kind == K::TemplateInstantiation) o = static_cast<const N::TemplateInstantiation*>(o)->m_template;
        const S::Symbol* owner = nullptr;
        if (o && o->kind == K::Identifier) owner = static_cast<const N::Identifier*>(o)->resolved;
        else if (o && o->kind == K::QualifiedIdentifier) owner = static_cast<const N::QualifiedIdentifier*>(o)->resolved;
        else if (o && o->kind == K::MemberAccessExpression) owner = static_cast<const N::MemberAccessExpression*>(o)->resolved;
        owner = follow(owner);
        if (!owner || (owner->kind != S::SymbolKind::Type && owner->kind != S::SymbolKind::Enum) || !owner->inner_scope) return nullptr;
        return owner->inner_scope->find_member(m->m_member);
    }

    std::string_view word_at(const char* at) const {
        if (!inside(at) || !word_start(*at)) return {};
        const char* e = at;
        while (e < m_end && (word_start(*e) || (*e >= '0' && *e <= '9'))) ++e;
        return std::string_view(at, static_cast<std::size_t>(e - at));
    }

    std::string_view op_text_at(const char* at, std::string_view text) const {
        if (!inside(at, text.size()) || std::string_view(at, text.size()) != text) return {};
        return std::string_view(at, text.size());
    }

    std::string_view callee_name(N::ASTNode* callee) const {
        if (!callee) return {};
        if (callee->kind == K::Identifier) return static_cast<N::Identifier*>(callee)->name;
        if (callee->kind == K::QualifiedIdentifier) {
            auto* q = static_cast<N::QualifiedIdentifier*>(callee);
            return q->parts().empty() ? std::string_view() : q->parts().back();
        }
        if (callee->kind == K::MemberAccessExpression) return static_cast<N::MemberAccessExpression*>(callee)->m_member;
        if (callee->kind == K::TemplateInstantiation) return callee_name(static_cast<N::TemplateInstantiation*>(callee)->m_template);
        return {};
    }

    std::size_t offset(const char* p) const { return static_cast<std::size_t>(p - m_begin); }

    static std::uint32_t utf16_units(const char* p, std::size_t n) {
        std::uint32_t units = 0;

        for (std::size_t i = 0; i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(p[i]);
            if ((c & 0xC0) == 0x80) continue;
            units += (c >= 0xF0) ? 2 : 1;
        }

        return units;
    }

    Position position(std::size_t at, std::size_t len) const {
        const auto it = std::upper_bound(m_line_starts.begin(), m_line_starts.end(), at);
        const std::size_t line = static_cast<std::size_t>(it - m_line_starts.begin()) - 1;
        const std::size_t start = m_line_starts[line];
        const std::size_t avail = static_cast<std::size_t>(m_end - (m_begin + at));
        Position p;
        p.line = static_cast<std::uint32_t>(line);
        p.col  = utf16_units(m_begin + start, at - start);
        p.len  = utf16_units(m_begin + at, std::min(len, avail));
        return p;
    }

    void add(std::string_view name, std::string_view kind, std::uint32_t mods, int rank) {
        if (name.empty() || kind.empty() || !inside(name.data(), name.size()) || !word_start(name.front())) return;
        const std::size_t at = offset(name.data());
        auto it = m_tokens.find(at);

        if (it == m_tokens.end()) {
            m_tokens.emplace(at, Entry{ static_cast<std::uint32_t>(name.size()), kind, mods, rank });
            return;
        }

        if (rank > it->second.rank) {
            it->second.kind = kind;
            it->second.mods = mods | (it->second.mods & Overloaded);
            it->second.rank = rank;
            it->second.len  = static_cast<std::uint32_t>(name.size());
        }
    }

    void use(std::string_view name, const S::Symbol* s, int rank = 1) {
        if (!s) return;
        add(name, kind_of(s), is_variable_template(s) ? VariableTemplate : 0, rank);
        if (kind_of(s) != "operator") link(name, s);
    }

    void declare(std::string_view name, const S::Symbol* s, std::string_view fallback, std::uint32_t mods = 0) {
        std::string_view kind = s ? kind_of(s) : std::string_view();
        if (kind.empty()) kind = fallback;
        if (is_variable_template(s)) mods |= VariableTemplate;
        add(name, kind, Declaration | mods, 3);
        if (s) link(name, s);
        if (s && !name.empty() && inside(name.data(), name.size())) m_declared.emplace_back(name, s);
    }

    void op_text(const char* at, std::string_view text) {
        if (!inside(at, text.size()) || std::string_view(at, text.size()) != text) return;
        const std::size_t o = offset(at);
        auto it = m_tokens.find(o);
        if (it == m_tokens.end()) m_tokens.emplace(o, Entry{ static_cast<std::uint32_t>(text.size()), "operator", Overloaded, 1 });
        else it->second.mods |= Overloaded;
    }

    std::string_view op_spelling(const char* at, tokenizing::Token::Kind kind) const {
        if (!inside(at)) return {};
        const tokenizing::Token t(kind, 0);
        std::string_view best;

        for (std::size_t i = 0; i < t.spelling_count(); ++i) {
            const std::string_view s = t.spellings()[i];
            if (s.size() > best.size() && inside(at, s.size()) && std::string_view(at, s.size()) == s) best = s;
        }

        if (best.empty()) {
            static constexpr std::string_view symbols[] = {
                "<<=", ">>=", "<=>", "**=", "->", "&&", "||", "==", "!=", "<=", ">=", "<<", ">>", "++", "--",
                "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "**", "+", "-", "*", "/", "%", "<", ">", "=", "!", "&", "|", "^", "~"
            };

            for (const std::string_view s : symbols) {
                if (inside(at, s.size()) && std::string_view(at, s.size()) == s) { best = s; break; }
            }
        }

        if (best.empty()) best = word_at(at);
        return best.empty() ? std::string_view() : std::string_view(at, best.size());
    }

    void op(const char* at, tokenizing::Token::Kind kind) {
        const std::string_view best = op_spelling(at, kind);
        if (!best.empty()) op_text(at, best);
    }

    void qualifiers(const std::vector<std::string_view>& parts, std::size_t count, const S::Symbol* last) {
        last = follow(last);
        const S::Scope* sc = last ? last->owner : nullptr;

        for (std::size_t i = count; i-- > 0;) {
            const S::Symbol* o = nullptr;

            for (int guard = 0; sc && guard < 8; ++guard) {
                o = sc->owner_symbol;
                if (o && o->name == parts[i]) break;
                o = nullptr;
                if (sc->kind != S::Scope::Kind::Template) break;
                sc = sc->parent;
            }

            if (!o) return;
            add(parts[i], kind_of(o), 0, 1);
            link(parts[i], o);
            describe(parts[i], o, nullptr, 1);
            sc = o->owner;
        }
    }

    void targs(const std::vector<parser_types::TemplateArgument*>& args) {
        for (parser_types::TemplateArgument* a : args) {
            if (!a) continue;
            if (a->is_type()) type(a->type);
            else walk(a->value);
        }
    }

    void type(const parser_types::TypeInfo& t) {
        if (t.type && t.type->is_user_defined()) {
            const auto* u = static_cast<const parser_types::UserDefinedType*>(t.type);
            const std::vector<std::string_view>& parts = u->parts();

            if (!parts.empty() && t.resolved) {
                use(parts.back(), t.resolved);
                describe(parts.back(), t.resolved, nullptr, 1);
                if (parts.size() > 1) qualifiers(parts, parts.size() - 1, t.resolved);
                const S::Symbol* r = follow(t.resolved);

                if (t.canonical && r) {
                    const std::string full = type_text(t.canonical);
                    const bool alias_says_it = r->kind == S::SymbolKind::TypeAlias && r->type && type_text(*r->type, m_types) == full;
                    if (full != qualified_name(r) && full != r->name && !alias_says_it) hover(parts.back(), "", full, "", "", 1);
                }
            }
        }

        targs(t.template_args);
    }
};

} // namespace

std::string highlight_json(CompilerPipeline& compiler) {
    const std::optional<FileId> main = compiler.load_input();
    const SourceManager& sources = compiler.get_sources();
    const bool semantic = !compiler.had_front_end_errors();

    if (!main) {
        Collector empty("", 0);
        return empty.json(compiler.get_reporter(), compiler.get_warnings(), INVALID_FILE, sources, false);
    }

    const std::size_t error_limit = compiler.get_reporter().errors().size();
    const std::size_t warning_limit = compiler.get_warnings().warnings().size();
    S::Instantiator& instantiator = compiler.mutable_instantiator();
    const bool was_frozen = instantiator.frozen();
    instantiator.freeze();
    Collector c(sources.data(*main), sources.size(*main), semantic ? &instantiator.evaluator() : nullptr, semantic ? &compiler.mutable_types() : nullptr, &sources, *main, semantic ? &instantiator : nullptr);
    const bool hid_random = instantiator.evaluator().hide_random;
    if (!S::compile_time_random_is_seeded()) instantiator.evaluator().hide_random = true;
    const auto imported = compiler.file_imports().find(*main);
    if (imported != compiler.file_imports().end()) c.set_imports(&imported->second);

    for (const CompilerPipeline::AnalyzedUnit& u : compiler.analyzed_units()) {
        if (u.file == *main && u.ast) c.walk(u.ast);
    }

    compiler.get_instantiator().for_each([&c](const S::Instantiator::Instantiation& I) {
        if (I.decl) c.walk_instantiation(I.decl);
    });

    c.words();
    instantiator.evaluator().hide_random = hid_random;
    instantiator.freeze(was_frozen);
    return c.json(compiler.get_reporter(), compiler.get_warnings(), *main, sources, semantic, error_limit, warning_limit);
}

namespace {

S::Scope* scope_of_node(N::ASTNode* n) {
    switch (n->kind) {
        case K::BlockStatement: return static_cast<N::BlockStatement*>(n)->scope;
        case K::IfBranch: return static_cast<N::IfBranch*>(n)->scope;
        case K::ForStatement: return static_cast<N::ForStatement*>(n)->scope;
        case K::SwitchStatement: return static_cast<N::SwitchStatement*>(n)->scope;
        case K::LambdaExpression: return static_cast<N::LambdaExpression*>(n)->scope;
        case K::RecordDeclaration: return static_cast<N::RecordDeclaration*>(n)->scope;
        case K::TemplateDeclaration: return static_cast<N::TemplateDeclaration*>(n)->scope;
        case K::NamespaceDeclaration: {
            const S::Symbol* sym = static_cast<N::NamespaceDeclaration*>(n)->symbol;
            return sym ? sym->inner_scope : nullptr;
        }
        default: return nullptr;
    }
}

struct Site {
    N::ASTNode* node = nullptr;
    S::Scope* scope = nullptr;
    std::uint32_t line = 0;
};

struct SiteFinder {
    std::string_view placeholder;
    std::uint32_t want_line = 0;
    Site found;
    Site best_line;
    std::uint32_t best_span = static_cast<std::uint32_t>(-1);

    std::uint32_t max_line(N::ASTNode* n, int depth = 0) {
        std::uint32_t m = n->line;
        if (depth > 400) return m;
        N::for_each_child(n, [&](N::ASTNode* c) { m = std::max(m, max_line(c, depth + 1)); });
        return m;
    }

    bool matches(N::ASTNode* n) const {
        switch (n->kind) {
            case K::Identifier: return static_cast<N::Identifier*>(n)->name == placeholder;
            case K::QualifiedIdentifier: {
                const auto& parts = static_cast<N::QualifiedIdentifier*>(n)->m_parts;
                return !parts.empty() && parts.back() == placeholder;
            }
            case K::MemberAccessExpression: return static_cast<N::MemberAccessExpression*>(n)->m_member == placeholder;
            default: return false;
        }
    }

    void walk(N::ASTNode* n, S::Scope* scope, int depth = 0) {
        if (!n || found.node || depth > 400) return;
        if (S::Scope* own = scope_of_node(n)) scope = own;
        if (matches(n)) {
            found = Site{ n, scope, n->line };
            return;
        }
        if (scope_of_node(n) && want_line > 0 && n->line <= want_line) {
            const std::uint32_t last = max_line(n) + 1;
            if (last >= want_line && last - n->line <= best_span) {
                best_span = last - n->line;
                best_line = Site{ n, scope, n->line };
            }
        }
        N::for_each_child(n, [&](N::ASTNode* c) { walk(c, scope, depth + 1); });
    }
};

std::string_view grove_kind(const S::Symbol* s) {
    const std::string_view k = kind_of(s);
    if (k == "property") return "field";
    if (k == "enumMember") return "constant";
    if (k == "typeParameter" || k == "concept") return "type";
    return k;
}

struct Candidates {
    S::TypeContext* types = nullptr;
    std::vector<std::string> order;
    std::map<std::string, std::pair<const S::Symbol*, int>> by_name;
    std::string_view placeholder;

    void add(const S::Symbol* raw) {
        if (!raw) return;
        const S::Symbol* s = follow(raw);
        if (!s || raw->name.empty() || raw->name == placeholder) return;
        const std::string name(raw->name);
        if (name[0] == '~' || name.rfind("operator", 0) == 0 || name.rfind("__", 0) == 0) return;
        if (s->decl && (s->decl->kind == K::ConstructorDeclaration || s->decl->kind == K::DestructorDeclaration || s->decl->kind == K::OperatorFunctionDeclaration)) return;
        if (kind_of(s).empty() || kind_of(s) == "operator") return;
        auto it = by_name.find(name);
        if (it != by_name.end()) {
            if (s->kind == S::SymbolKind::Function && it->second.first->kind == S::SymbolKind::Function && it->second.first != s) ++it->second.second;
            return;
        }
        int overloads = 0;
        for (const S::Symbol* o = s->next_overload; o && overloads < 64; o = o->next_overload) ++overloads;
        by_name.emplace(name, std::make_pair(s, overloads));
        order.push_back(name);
    }

    void members(const S::Scope* scope, int depth = 0) {
        if (!scope || depth > 16) return;
        for (const S::Symbol* s : scope->symbols) add(s);
        for (const S::Scope* b : scope->bases) members(b, depth + 1);
    }

    std::string detail(const S::Symbol* s) const {
        switch (s->kind) {
            case S::SymbolKind::Function: return function_signature(s, types);
            case S::SymbolKind::Variable:
            case S::SymbolKind::Parameter: return s->type ? type_text(*s->type, types) : std::string();
            case S::SymbolKind::Type:
            case S::SymbolKind::Enum:
            case S::SymbolKind::Namespace:
            case S::SymbolKind::Module:
            case S::SymbolKind::Concept: return std::string(kind_of(s));
            default: return std::string();
        }
    }

    std::string json(std::size_t start, std::string_view mode) const {
        std::string out = "{\"start\":" + std::to_string(start) + ",\"mode\":\"" + std::string(mode) + "\",\"items\":[";
        bool first = true;
        for (const std::string& name : order) {
            const auto& [s, overloads] = by_name.at(name);
            std::string d = detail(s);
            if (overloads > 0) d += " (+" + std::to_string(overloads) + " overload" + (overloads == 1 ? "" : "s") + ")";
            if (!first) out += ",";
            first = false;
            out += "{\"label\":" + json_string(name) + ",\"kind\":" + json_string(grove_kind(s)) + ",\"detail\":" + json_string(d) + "}";
        }
        out += "]}\n";
        return out;
    }
};

S::Type* receiver_type(S::Type* t, S::TypeContext& types, bool arrow) {
    for (int i = 0; t && i < 8; ++i) {
        if (t->is_reference()) {
            t = static_cast<S::ReferenceType*>(t)->referent();
            continue;
        }
        break;
    }
    t = types.strip_cv(t);
    if (arrow && t && t->is_pointer()) t = types.strip_cv(static_cast<S::PointerType*>(t)->pointee());
    return t;
}

const S::Scope* resolve_qualifier(const S::Scope* scope, const std::vector<std::string_view>& parts) {
    if (!scope || parts.empty()) return nullptr;
    const S::Symbol* s = follow(scope->resolve(parts[0]));
    for (std::size_t i = 1; s && i < parts.size(); ++i) {
        if (!s->inner_scope) return nullptr;
        s = follow(s->inner_scope->find_member(parts[i]));
    }
    return s ? s->inner_scope : nullptr;
}

} // namespace

std::string complete_json(CompilerPipeline& compiler, std::string_view placeholder, const CompletionRequest& request) {
    Candidates cands;
    cands.placeholder = placeholder;
    cands.types = &compiler.mutable_types();
    const std::optional<FileId> main = compiler.load_input();
    if (!main) return cands.json(request.start, "none");
    SiteFinder finder;
    finder.placeholder = placeholder;
    finder.want_line = request.line;
    S::Scope* root = nullptr;
    for (const CompilerPipeline::AnalyzedUnit& u : compiler.analyzed_units()) {
        if (u.file != *main || !u.ast) continue;
        root = u.root;
        finder.walk(u.ast, u.root);
        if (finder.found.node) break;
    }
    const Site site = finder.found.node ? finder.found : finder.best_line.node ? finder.best_line : Site{ nullptr, root, 0 };
    const S::Scope* scope = site.scope ? site.scope : root;
    const char* caret = site.node ? site.node->at : nullptr;
    std::string_view mode = "name";

    if (request.mode == CompletionRequest::Mode::Member || request.mode == CompletionRequest::Mode::Arrow) {
        mode = "member";
        const S::Scope* members = nullptr;
        if (site.node && site.node->kind == K::MemberAccessExpression) {
            const auto* m = static_cast<N::MemberAccessExpression*>(site.node);
            S::Type* t = m->m_object ? m->m_object->expr_type.type : nullptr;
            t = receiver_type(t, compiler.mutable_types(), request.mode == CompletionRequest::Mode::Arrow || m->is_arrow());
            if (t) members = S::record_scope_of(t, compiler.mutable_types());
        }
        if (!members && !request.qualifier.empty() && scope) {
            const S::Symbol* v = follow(scope->resolve(request.qualifier.front()));
            if (v && (v->kind == S::SymbolKind::Variable || v->kind == S::SymbolKind::Parameter) && v->type) {
                const S::Symbol* type_sym = follow(scope->resolve(type_text(*v->type, nullptr)));
                if (type_sym) members = type_sym->inner_scope;
            }
        }
        cands.members(members);
        return cands.json(request.start, mode);
    }

    if (request.mode == CompletionRequest::Mode::Scope) {
        mode = "scope";
        const S::Scope* inner = nullptr;
        if (site.node && site.node->kind == K::MemberAccessExpression) {
            const auto* m = static_cast<N::MemberAccessExpression*>(site.node);
            const S::Symbol* base = nullptr;
            if (m->m_object && m->m_object->kind == K::Identifier) base = static_cast<N::Identifier*>(m->m_object)->resolved;
            else if (m->m_object && m->m_object->kind == K::QualifiedIdentifier) base = static_cast<N::QualifiedIdentifier*>(m->m_object)->resolved;
            base = follow(base);
            if (base) inner = base->inner_scope;
        }
        if (!inner) {
            std::vector<std::string_view> parts(request.qualifier.begin(), request.qualifier.end());
            for (const S::Scope* s = scope; s && !inner; s = s->parent) inner = resolve_qualifier(s, parts);
        }
        if (inner) {
            for (const S::Symbol* s : inner->symbols) cands.add(s);
            for (const S::Scope* b : inner->bases) cands.members(b);
        }
        return cands.json(request.start, mode);
    }

    for (const S::Scope* s = scope; s; s = s->parent) {
        const bool local = s->kind == S::Scope::Kind::Block || s->kind == S::Scope::Kind::Function || s->kind == S::Scope::Kind::Lambda;
        for (const S::Symbol* sym : s->symbols) {
            if (local && caret && sym->decl && sym->decl->file_id == *main && sym->decl->at && sym->decl->at > caret) continue;
            cands.add(sym);
        }
        if (s->kind == S::Scope::Kind::Record) {
            for (const S::Scope* b : s->bases) cands.members(b);
        }
    }
    return cands.json(request.start, mode);
}

} // namespace driver
} // namespace walnut
