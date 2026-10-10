#include "Parser/Nodes/statement_nodes.hpp"

namespace walnut {
namespace nodes {

VariableDeclaration::VariableDeclaration(const parser_types::TypeInfo& type_info, std::string_view name, ASTNode* init, std::uint32_t ln) : ASTNode(Kind::VariableDeclaration, ln), m_type_info(type_info), m_name(name), m_initializer(init) {}

VariableDeclaration::VariableDeclaration(const parser_types::TypeInfo& type_info, SmallVector<std::string_view, 4> bindings, ASTNode* init, std::uint32_t ln) : ASTNode(Kind::VariableDeclaration, ln), m_type_info(type_info), m_bindings(std::move(bindings)), m_initializer(init) {}

void VariableDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "VariableDeclaration\n";
    print_indent(os, indent + 1);
        
    if (is_structured_binding()) {
        os << "Bindings: [";
        for (std::size_t i = 0; i < m_bindings.size(); ++i) { if (i) { os << ", "; } os << m_bindings[i]; }
        os << "]";
    } else {
        os << "Name: " << m_name;
    }

    os << "\n";
    print_indent(os, indent + 1);
    os << "Type: " << m_type_info << "\n";
    print_indent(os, indent + 1);
    os << "Initializer:";

    if (!m_initializer) {
        os << " <none>\n";
    } else {
        os << "\n";
        print_node(m_initializer, os, indent + 2);
    }
}

ASTNode* VariableDeclaration::clone_into(Arena& a) const {
    VariableDeclaration* c;
    if (is_structured_binding()) c = make_in<VariableDeclaration>(a, m_type_info.clone_into(a), m_bindings, clone_child(m_initializer, a), line);
    else                         c = make_in<VariableDeclaration>(a, m_type_info.clone_into(a), m_name,     clone_child(m_initializer, a), line);
    copy_base_to(c);
    return c;
}

void BlockStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "Block\n";
    for (const auto& stmt : statements) { print_node(stmt, os, indent + 1); }
}

ASTNode* BlockStatement::clone_into(Arena& a) const { auto* c = make_in<BlockStatement>(a); copy_base_to(c); c->statements = clone_list(statements, a); return c; }

void IfBranch::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << (is_constexpr ? "If Constexpr Branch\n" : "If Branch\n");
    print_indent(os, indent);
    os << "Condition:\n";
    print_node(condition, os, indent + 1);
    print_indent(os, indent);
    os << "Body:\n";
    print_node(body, os, indent + 1);
}

ASTNode* IfBranch::clone_into(Arena& a) const { auto* c = make_in<IfBranch>(a, clone_child(condition, a), clone_child(body, a)); copy_base_to(c); c->is_constexpr = is_constexpr; return c; }

void IfStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "IfStatement\n";

    for (std::size_t i = 0; i < branches.size(); ++i) {
        print_indent(os, indent + 1);
        os << (i == 0 ? "If" : "Else If") << " Branch:\n";
        branches[i]->print(os, indent + 2);
    }

    if (else_branch) {
        print_indent(os, indent + 1);
        os << "Else Branch:\n";
        print_node(else_branch, os, indent + 2);
    }
}

ASTNode* IfStatement::clone_into(Arena& a) const {
    auto* c = make_in<IfStatement>(a);
    copy_base_to(c);
    c->branches    = clone_typed_list(branches, a);
    c->else_branch = clone_child(else_branch, a);
    return c;
}

NamespaceDeclaration::NamespaceDeclaration(
        std::vector<std::string_view> name_parts,
        BlockStatement* body,
        std::uint32_t ln
) : ASTNode(Kind::NamespaceDeclaration, ln)
        , m_name_parts(std::move(name_parts))
        , m_body(body)
    {}

std::string NamespaceDeclaration::full_name() const {
    std::string result;

    for (std::size_t i = 0; i < m_name_parts.size(); ++i) {
        if (i > 0) result += "::";
        result += m_name_parts[i];
    }

    return result;
}

void NamespaceDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "NamespaceDeclaration\n";
    print_indent(os, indent + 1);
    os << "Name: " << full_name() << "\n";
    print_indent(os, indent + 1);
    os << "Body:";

    if (!m_body) { 
        os << " <none>\n"; 
    } else {
        os << "\n";
        m_body->print(os, indent + 2);
    }
}

ASTNode* NamespaceDeclaration::clone_into(Arena& a) const { auto* c = make_in<NamespaceDeclaration>(a, m_name_parts, clone_typed(m_body, a), line); copy_base_to(c); return c; }

ForStatement::ForStatement(ASTNode* init, ASTNode* cond, ASTNode* inc, ASTNode* b) : ASTNode(Kind::ForStatement)
        , initializer(init), condition(cond), increment(inc)
        , body(b), has_var_init(false) {}

ForStatement::ForStatement(ASTNode* var_initializer, std::nullptr_t, ASTNode* cond, ASTNode* inc, ASTNode* b) : ASTNode(Kind::ForStatement)
        , condition(cond), increment(inc), body(b)
        , var_init(var_initializer), has_var_init(true) {}

void ForStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "ForStatement\n";
    print_indent(os, indent + 1);
    os << "Initializer:\n";

    if (has_var_init) {
        print_node(var_init, os, indent + 2);
    } else if (initializer) {
        print_node(initializer, os, indent + 2);
    } else {
        print_indent(os, indent + 2);
        os << "<empty>\n";
    }

    print_indent(os, indent + 1);
    os << "Condition:\n";

    if (condition) {
        print_node(condition, os, indent + 2);
    } else {
        print_indent(os, indent + 2);
        os << "<empty>\n";
    }

    print_indent(os, indent + 1);
    os << "Increment:\n";

    if (increment) {
        print_node(increment, os, indent + 2);
    } else {
        print_indent(os, indent + 2);
        os << "<empty>\n";
    }

    print_indent(os, indent + 1);
    os << "Body:\n";
    print_node(body, os, indent + 2);
}

ASTNode* ForStatement::clone_into(Arena& a) const {
    ForStatement* c;
    if (has_var_init) c = make_in<ForStatement>(a, clone_child(var_init, a), nullptr, clone_child(condition, a), clone_child(increment, a), clone_child(body, a));
    else              c = make_in<ForStatement>(a, clone_child(initializer, a), clone_child(condition, a), clone_child(increment, a), clone_child(body, a));
    copy_base_to(c);
    return c;
}

ForEachStatement::ForEachStatement(const parser_types::TypeInfo& element_type, const modifiers::RawModifiers& mods, std::string_view var_name, ASTNode* container, ASTNode* body, std::uint32_t ln) : ASTNode(Kind::ForEachStatement, ln), m_element_type(element_type), m_modifiers(mods)
        , m_var_name(var_name), m_container(container), m_body(body) {}

ForEachStatement::ForEachStatement(const parser_types::TypeInfo& element_type, const modifiers::RawModifiers& mods, SmallVector<std::string_view, 4> bindings, ASTNode* container, ASTNode* body, std::uint32_t ln) : ASTNode(Kind::ForEachStatement, ln), m_element_type(element_type), m_modifiers(mods)
        , m_bindings(std::move(bindings)), m_container(container), m_body(body) {}

void ForEachStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "ForEachStatement\n";
    print_indent(os, indent + 1);

    if (is_structured_binding()) {
        os << "Bindings: [";
        for (std::size_t i = 0; i < m_bindings.size(); ++i) { if (i) { os << ", "; } os << m_bindings[i]; }
        os << "]\n";
    } else {
        os << "Variable: " << m_var_name << "\n";
    }

    print_indent(os, indent + 1);
    os << "Type: " << m_element_type << "\n";
    print_indent(os, indent + 1);
    os << "Modifiers: " << m_modifiers << "\n";
    print_indent(os, indent + 1);
    os << "Container:\n";
    print_node(m_container, os, indent + 2);
    print_indent(os, indent + 1);
    os << "Body:\n";
    print_node(m_body, os, indent + 2);
}

ASTNode* ForEachStatement::clone_into(Arena& a) const {
    ForEachStatement* c;
    if (is_structured_binding()) c = make_in<ForEachStatement>(a, m_element_type.clone_into(a), m_modifiers, m_bindings, clone_child(m_container, a), clone_child(m_body, a), line);
    else                         c = make_in<ForEachStatement>(a, m_element_type.clone_into(a), m_modifiers, m_var_name, clone_child(m_container, a), clone_child(m_body, a), line);
    copy_base_to(c);
    return c;
}

void WhileStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "WhileStatement\n";
    print_indent(os, indent + 1);
    os << "Condition:\n";
    print_node(condition, os, indent + 2);
    print_indent(os, indent + 1);
    os << "Body:\n";
    print_node(body, os, indent + 2);
}

ASTNode* WhileStatement::clone_into(Arena& a) const { auto* c = make_in<WhileStatement>(a, clone_child(body, a), clone_child(condition, a)); copy_base_to(c); return c; }

ArrayDeclaration::ArrayDeclaration(
        std::string_view name,
        const parser_types::TypeInfo& element_type,
        const modifiers::RawModifiers& array_mods,
        std::uint32_t ln,
        std::optional<std::size_t> dimension,
        ASTNode* dimension_expr,
        BraceInitializerList* initializer 
) : ASTNode(Kind::ArrayDeclaration, ln)
        , m_name(name)
        , m_element_type(element_type)
        , m_array_modifiers(array_mods)
        , m_dimension(dimension)
        , m_dimension_expr(dimension_expr)
        , m_initializer(initializer)
    {}

void ArrayDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "ArrayDeclaration\n";
    print_indent(os, indent + 1);
    os << "Name: " << m_name << "\n";
    print_indent(os, indent + 1);
    os << "Element Type: " << m_element_type << "\n";
    print_indent(os, indent + 1);
    os << "Array Modifiers: ";

    if (!m_array_modifiers.is_modified()) {
        os << "<none>\n";
    } else {
        os << m_array_modifiers << "\n";
    }

    print_indent(os, indent + 1);
    os << "Alignas: " << (m_alignment ? (m_alignment->is_type() ? "<type>" : "<value>") : "<none>") << "\n";
    print_indent(os, indent + 1);
    os << "Dimension: ";

    if (m_dimension.has_value()) {
        os << m_dimension.value() << "\n";
    } else if (m_dimension_expr) {
        os << "<dynamic>\n";
        print_node(m_dimension_expr, os, indent + 2);
    } else {
        os << "<inferred>\n";
    }

    print_indent(os, indent + 1);
    os << "Initializer:";
        
    if (!m_initializer) { 
        os << " <none>\n"; 
    } else {
        os << "\n";
        m_initializer->print(os, indent + 2);
    } 
}

ASTNode* ArrayDeclaration::clone_into(Arena& a) const {
    auto* c = make_in<ArrayDeclaration>(
        a, m_name, 
        m_element_type.clone_into(a), 
        m_array_modifiers, 
        line,
        m_dimension, 
        clone_child(m_dimension_expr, a), 
        clone_typed(m_initializer, a)
    );
        
    copy_base_to(c);
    c->m_alignment = clone_targ(m_alignment, a);
    return c;
}

const char* SingleStatement::variant_name() const {
    switch (variant) {
        case Variant::Break:       return "break";
        case Variant::Continue:    return "continue";
        case Variant::Fallthrough: return "fallthrough";
        case Variant::Repeat:      return "repeat";
    }
    return "unknown";
}

void DoWhileStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "DoWhileStatement\n";
    print_indent(os, indent + 1);
    os << "Body:\n";
    print_node(body, os, indent + 2);
    print_indent(os, indent + 1);
    os << "Condition:\n";
    print_node(condition, os, indent + 2);
}

ASTNode* DoWhileStatement::clone_into(Arena& a) const { auto* c = make_in<DoWhileStatement>(a, clone_child(body, a), clone_child(condition, a), line); copy_base_to(c); return c; }

void SwitchCase::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << (is_default() ? "DefaultCase" : "SwitchCase") << "\n";

    if (value) {
        print_indent(os, indent + 1);
        os << "Value:\n";
        print_node(value, os, indent + 2);
    }

    print_indent(os, indent + 1);
    os << "Body:\n";
    for (const auto& stmt : body) { print_node(stmt, os, indent + 2); }
    print_indent(os, indent + 1);
    os << "Fallthrough: " << (has_fallthrough ? "true" : "false") << "\n";
}

ASTNode* SwitchCase::clone_into(Arena& a) const {
    auto* c = make_in<SwitchCase>(a, clone_child(value, a), line);
    copy_base_to(c);
    c->body = clone_list(body, a);
    c->has_fallthrough = has_fallthrough;
    return c;
}

void SwitchStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "SwitchStatement\n";
    print_indent(os, indent + 1);
    os << "Condition:\n";
    print_node(condition, os, indent + 2);
    for (const auto& c : cases) { c->print(os, indent + 1); }
}

ASTNode* SwitchStatement::clone_into(Arena& a) const {
    auto* c = make_in<SwitchStatement>(a, clone_child(condition, a), line);
    copy_base_to(c);
    c->cases = clone_typed_list(cases, a);
    return c;
}

TryCatchStatement::TryCatchStatement(
        BlockStatement* tb,
        std::string_view cn,
        const parser_types::TypeInfo& ct,
        bool typed,
        BlockStatement* cb,
        std::uint32_t ln 
) : ASTNode(Kind::TryCatchStatement, ln)
        , try_body(tb), catch_name(cn), catch_type(ct)
        , has_typed_catch(typed), catch_body(cb)
    {}

void TryCatchStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "TryCatchStatement\n";
    print_indent(os, indent + 1);
    os << "Try Body:\n";
    if (try_body) try_body->print(os, indent + 2);
    print_indent(os, indent + 1);
    os << "Catch";

    if (has_typed_catch) {
        os << " (" << catch_type << " " << catch_name << "):\n";
    } else {
        os << " (" << catch_name << "):\n";
    }

    catch_body->print(os, indent + 2);
    if (next_handler) next_handler->print(os, indent);
}

ASTNode* TryCatchStatement::clone_into(Arena& a) const {
    auto* c = make_in<TryCatchStatement>(a, try_body ? clone_typed(try_body, a) : nullptr, catch_name, catch_type.clone_into(a), has_typed_catch, clone_typed(catch_body, a), line);
    copy_base_to(c);
    if (next_handler) c->next_handler = static_cast<TryCatchStatement*>(next_handler->clone_into(a));
    return c;
}

void EnumValue::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "EnumValue\n";
    print_indent(os, indent + 1);
    os << "Name: " << name << "\n";
    print_indent(os, indent + 1);
    os << "Initializer:";

    if (!initializer) {
        os << " <none>\n"; 
    } else {
        os << "\n";
        print_node(initializer, os, indent + 2);
    }
}

ASTNode* EnumValue::clone_into(Arena& a) const { auto* c = make_in<EnumValue>(a, name, clone_child(initializer, a), line); copy_base_to(c); return c; }

EnumDeclaration::EnumDeclaration(
        std::string_view n,
        const parser_types::TypeInfo& ut,
        std::uint32_t ln 
) : ASTNode(Kind::EnumDeclaration, ln)
        , name(n)
        , underlying_type(ut)
        , has_underlying_type(true)
    {}

void EnumDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "EnumDeclaration\n";
    print_indent(os, indent + 1);
    os << "Name: " << name << "\n";
    print_indent(os, indent + 1);
    os << "Underlying Type: ";

    if (has_underlying_type) {
        os << underlying_type << "\n";
    } else {
        os << "<default>\n";
    }

    print_indent(os, indent + 1);
    os << "Modifiers: " << m_modifiers << "\n";
    print_indent(os, indent + 1);
    os << "Values:";

    if (values.empty()) { 
        os << " <none>\n"; 
    } else {
        os << "\n";
        for (auto* v : values) { v->print(os, indent + 2); }
    }
}

ASTNode* EnumDeclaration::clone_into(Arena& a) const {
    EnumDeclaration* c;
    if (has_underlying_type) c = make_in<EnumDeclaration>(a, name, underlying_type.clone_into(a), line);
    else                     c = make_in<EnumDeclaration>(a, name, line);
    copy_base_to(c);
    c->m_modifiers = m_modifiers;
    c->values      = clone_typed_list(values, a);
    return c;
}

UsingDeclaration::UsingDeclaration(std::string_view n, const parser_types::TypeInfo& type, std::uint32_t ln) : ASTNode(Kind::UsingDeclaration, ln), variant(Variant::Typedef), name(n), aliased_type(type) {}

UsingDeclaration::UsingDeclaration(std::vector<std::string_view> parts, std::uint32_t ln) : ASTNode(Kind::UsingDeclaration, ln), variant(Variant::NamespaceDirective), target_parts(std::move(parts)) {}

UsingDeclaration::UsingDeclaration(std::string_view n, std::vector<std::string_view> parts, bool global, std::uint32_t ln) : ASTNode(Kind::UsingDeclaration, ln), variant(Variant::NamespaceAlias), name(n), target_parts(std::move(parts)), target_global(global) {}

const char* UsingDeclaration::variant_name() const {
    switch (variant) {
        case Variant::Alias:              return "using-alias";
        case Variant::Typedef:            return "typedef";
        case Variant::NamespaceDirective: return "using-namespace";
        case Variant::NamespaceAlias:     return "namespace-alias";
    }

    return "unknown";
}

std::string UsingDeclaration::target_name() const {
    std::string result = target_global ? "::" : "";

    for (std::size_t i = 0; i < target_parts.size(); ++i) {
        if (i > 0) result += "::";
        result += target_parts[i];
    }

    return result;
}

void UsingDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "UsingDeclaration (" << variant_name() << ")\n";

    switch (variant) {
        case Variant::Alias:
            print_indent(os, indent + 1); os << "Name: " << name << "\n";
            print_indent(os, indent + 1); os << "Aliases:\n";
            print_node(aliased_expr, os, indent + 2);
            break;
        case Variant::Typedef:
            print_indent(os, indent + 1); os << "Name: " << name << "\n";
            print_indent(os, indent + 1); os << "Aliases: " << aliased_type << "\n";
            break;
        case Variant::NamespaceDirective:
            print_indent(os, indent + 1); os << "Target: " << target_name() << "\n";
            break;
        case Variant::NamespaceAlias:
            print_indent(os, indent + 1); os << "Name: " << name << "\n";
            print_indent(os, indent + 1); os << "Target: " << target_name() << "\n";
            break;
    }
}

ASTNode* UsingDeclaration::clone_into(Arena& a) const {
    UsingDeclaration* c;

    switch (variant) {
        case Variant::Alias:          c = make_in<UsingDeclaration>(a, name, clone_child(aliased_expr, a), line); break;
        case Variant::Typedef:        c = make_in<UsingDeclaration>(a, name, aliased_type.clone_into(a), line);   break;
        case Variant::NamespaceAlias: c = make_in<UsingDeclaration>(a, name, target_parts, target_global, line);  break;
        default:                      c = make_in<UsingDeclaration>(a, target_parts, line);                       break;
    }

    c->target_global = target_global;
    copy_base_to(c);
    return c;
}

void TemplateParameter::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "TemplateParameter (" << (is_type_param() ? "type" : "non-type") << ")\n";

    if (is_type_param()) {
        print_indent(os, indent + 1);
        if (m_is_constrained) { os << "constraint: " << m_constraint << (m_is_pack ? " ..." : "") << "\n"; }
        else                  { os << "typename"     << (m_is_pack ? " ..." : "") << "\n"; }
    } else {
        print_indent(os, indent + 1);
        os << "Type: " << m_type << (m_is_pack ? " ..." : "") << "\n";
    }

    print_indent(os, indent + 1);
    os << "Name: " << (m_name.empty() ? "<unnamed>" : m_name) << "\n";
    print_indent(os, indent + 1);
    os << "Default: ";

    if (is_type_param() && m_has_default_type) {
        os << m_default_type << "\n";
    } else if (is_non_type_param() && m_default_value) {
        os << "\n";
        print_node(m_default_value, os, indent + 2);
    } else {
        os << "<none>\n";
    }
}

ASTNode* TemplateParameter::clone_into(Arena& a) const {
    auto* c = make_in<TemplateParameter>(a, line);
    copy_base_to(c);
    c->m_form             = m_form;
    c->m_name             = m_name;
    c->m_is_pack          = m_is_pack;
    c->m_type             = m_type.clone_into(a);
    c->m_default_type     = m_default_type.clone_into(a);
    c->m_has_default_type = m_has_default_type;
    c->m_default_value    = clone_child(m_default_value, a);
    c->m_is_constrained   = m_is_constrained;
    c->m_constraint       = m_constraint.clone_into(a);
    return c;
}

void TemplateDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "TemplateDeclaration" << (m_is_empty_template ? " (template <>)" : "") << "\n";
    print_indent(os, indent + 1);
    os << "Parameters:";

    if (m_params.empty()) {
        os << (m_is_empty_template ? " <empty>\n" : " <none>\n");
    } else {
        os << "\n";
        for (auto* p : m_params) { p->print(os, indent + 2); }
    }

    print_indent(os, indent + 1); os << "Requires:";
        
    if (m_requires_clause) {
        os << "\n";
        print_node(m_requires_clause, os, indent + 2);
    } else {
        os << " <none>\n";
    }

    print_indent(os, indent + 1);
    os << "Declaration:\n";
    print_node(m_declaration, os, indent + 2);
}

ASTNode* TemplateDeclaration::clone_into(Arena& a) const {
    auto* c = make_in<TemplateDeclaration>(a, line);
    copy_base_to(c);
    c->m_params           = clone_typed_list(m_params, a);
    c->m_declaration      = clone_child(m_declaration, a);
    c->m_is_empty_template = m_is_empty_template;
    c->m_requires_clause  = clone_child(m_requires_clause, a);
    return c;
}

const char* ImportExportItem::kind_name(Kind k) {
    switch (k) {
        case Kind::Name:      return "name";
        case Kind::Function:  return "function";
        case Kind::Namespace: return "namespace";
        case Kind::Module:    return "module";
        case Kind::File:      return "file";
        case Kind::This:      return "this";
    }
    return "unknown";
}

void print_import_export_item(std::ostream& os, const ImportExportItem& it, std::size_t indent) {
    auto write_qualified = [&os](bool glob, const std::vector<std::string_view>& parts) {
        if (glob) { os << "::"; }
        for (std::size_t i = 0; i < parts.size(); ++i) { if (i) { os << "::"; } os << parts[i]; }
    };

    print_indent(os, indent);
    os << "Item (" << ImportExportItem::kind_name(it.kind) << ")" << (it.is_template ? " [template]" : "") << ":\n";

    switch (it.kind) {
        case ImportExportItem::Kind::Function:
            print_node(it.decl, os, indent + 1);
            break;
        case ImportExportItem::Kind::Module:
            print_indent(os, indent + 1); os << "Name: " << it.source << "\n";
            break;
        case ImportExportItem::Kind::File:
            print_indent(os, indent + 1); os << "Path: " << it.source << "\n";
            print_indent(os, indent + 1); os << "As: "; write_qualified(it.alias_global, it.alias_parts); os << "\n";
            break;
        case ImportExportItem::Kind::This:
            print_indent(os, indent + 1); os << "&this\n";
            break;
        default: 
            print_indent(os, indent + 1); os << "Target: "; write_qualified(it.target_global, it.target_parts); os << "\n";
            if (it.has_source) { print_indent(os, indent + 1); os << "From: " << it.source << "\n"; }
            if (it.has_alias)  { print_indent(os, indent + 1); os << "As: "; write_qualified(it.alias_global, it.alias_parts); os << "\n"; }
            break;
    }
}

void ImportExportDeclaration::print(std::ostream& os, std::size_t indent) const {
    auto write_qualified = [&os](bool glob, const std::vector<std::string_view>& parts) {
        if (glob) os << "::";

        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) os << "::";
            os << parts[i];
        }
    };

    print_indent(os, indent);
    os << (is_import() ? "ImportDeclaration" : "ExportDeclaration") << (is_block ? " (block)" : "") << "\n";
    for (const auto& it : items) { for (const auto& it : items) { print_import_export_item(os, it, indent + 1); }}
}

ASTNode* ImportExportDeclaration::clone_into(Arena& a) const {
    auto* c = make_in<ImportExportDeclaration>(a, direction, is_block, line);
    copy_base_to(c);
    c->items.reserve(items.size());
    for (const auto& it : items) { ImportExportItem ni = it; ni.decl = clone_child(it.decl, a); c->items.push_back(std::move(ni)); }
    return c;
}

void ModuleDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "ModuleDeclaration\n";
    print_indent(os, indent + 1); os << "Name: " << name << "\n";
    print_indent(os, indent + 1); os << "Items (" << items.size() << "):";
    if (items.empty()) { os << " <none>\n"; return; }
    os << "\n";
    for (const auto& it : items) { print_import_export_item(os, it, indent + 2); }
}

ASTNode* ModuleDeclaration::clone_into(Arena& a) const {
    auto* c = make_in<ModuleDeclaration>(a, name, line);
    copy_base_to(c);
    c->items.reserve(items.size());
    for (const auto& it : items) { ImportExportItem ni = it; ni.decl = clone_child(it.decl, a); c->items.push_back(std::move(ni)); }
    return c;
}

void CoReturnStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "CoReturnStatement\n";
    if (m_value) { print_node(m_value, os, indent + 1); }
    else { print_indent(os, indent + 1); os << "<void>\n"; }
}

ASTNode* CoReturnStatement::clone_into(Arena& a) const { auto* c = make_in<CoReturnStatement>(a, clone_child(m_value, a), line); copy_base_to(c); return c; }

void ConceptDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "ConceptDeclaration\n";
    print_indent(os, indent + 1); 
    os << "Name: " << name << "\n";
    print_indent(os, indent + 1); 
    os << "Constraint:\n";
    print_node(constraint, os, indent + 2);
}

ASTNode* ConceptDeclaration::clone_into(Arena& a) const { auto* c = make_in<ConceptDeclaration>(a, name, clone_child(constraint, a), line); copy_base_to(c); return c; }

void StaticAssertDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "StaticAssert\n";
    print_indent(os, indent + 1); 
    os << "Message: " << message << "\n";
    print_indent(os, indent + 1); 
    os << "Constraint:\n";
    print_node(constraint, os, indent + 2);
}

ASTNode* StaticAssertDeclaration::clone_into(Arena& a) const { auto* c = make_in<StaticAssertDeclaration>(a, message, clone_child(constraint, a), line); copy_base_to(c); return c; }

} // namespace nodes
} // namespace walnut
