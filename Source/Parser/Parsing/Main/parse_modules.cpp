#include "Parser/Parsing/Main/parse_modules.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_module_declaration() {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    expect(K::ModuleKeyword, "Expected 'module'");

    if (current_token().kind() != K::Identifier) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Expected a module name after 'module'");
    }
    
    std::string_view name = current_token().lexeme();

    if (is_keyword(name)) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Module name cannot be a keyword");
    }

    advance();
    expect(K::LeftCurly, "Expected '{' to start module body");
    auto* decl = make<nodes::ModuleDeclaration>(name, static_cast<std::uint32_t>(line));
    while (!concrete_match(K::RightCurly) && !concrete_match(K::End)) { decl->add_item(parse_import_export_item(false)); }
    expect(K::RightCurly, "Expected '}' to close module body");
    return decl;
}

nodes::ImportExportItem Parser::parse_import_export_item(bool is_import) {
    using K  = tokenizing::Token::Kind;
    using IK = nodes::ImportExportItem::Kind;
    nodes::ImportExportItem item;
    item.line = current_token().line();

    auto read_qualified = [&](std::vector<std::string_view>& out, bool& glob, const char* ctx) {
        glob = match(K::DoubleColon);

        if (current_token().kind() != K::Identifier) {
            throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, ctx);
        }

        out.push_back(current_token().lexeme());
        advance();

        while (current_token().kind() == K::DoubleColon) {
            advance();

            if (current_token().kind() != K::Identifier) {
                throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Expected identifier after '::' in qualified name");
            }

            out.push_back(current_token().lexeme());
            advance();
        }
    };

    auto read_string = [&](const char* ctx) -> std::string_view {
        if (current_token().kind() != K::String) {
            throw ParserError::unexpected_token(reporter, current_token(), {K::String}, ctx);
        }

        validate_string_literal(current_token());
        std::string_view s = current_token().lexeme();
        advance();
        return s;
    };

    if (match(K::TemplateKeyword)) { item.is_template = true; }

    if (current_token().kind() == K::FunctionKeyword) {
        if (is_import) {
            throw ParserError::invalid_expression(reporter, current_token(), "Cannot import a function declaration; import the function by name instead");
        }

        item.kind = IK::Function;
        item.decl = parse_function_declaration({}, current_token().line());  
        return item;
    }

    if (match(K::NamespaceKeyword)) {
        item.kind = IK::Namespace;
        read_qualified(item.target_parts, item.target_global, is_import ? "Expected a namespace name to import" : "Expected a namespace name to export");
        
        if (is_import) {
            expect(K::FromKeyword, "Expected 'from' in namespace import");
            item.has_source = true;
            item.source = read_string("Expected a module path string after 'from'");
        }

        if (match(K::AsKeyword)) {
            item.has_alias = true;
            read_qualified(item.alias_parts, item.alias_global, "Expected an alias namespace after 'as'");
        }

        return item;
    }

    if (match(K::ModuleKeyword)) {
        item.kind = IK::Module;
        item.target_parts.push_back(read_string("Expected a module name string after 'module'"));

        if (is_import) {
            expect(K::FromKeyword, "Expected 'from' in module import");
            item.has_source = true;
            item.source = read_string("Expected a module path string after 'from'");
        }

        return item;
    }

    if (match(K::AtSymbol)) {
        if (is_import) {
            item.kind = IK::File;
            item.has_source = true;
            item.source = read_string("Expected a file path string after '@'");

            if (!match(K::AsKeyword)) {
                throw ParserError::missing_token(reporter, current_token(), K::AsKeyword, "import @\"...\" requires 'as <name>'");
            }

            item.has_alias = true;
            read_qualified(item.alias_parts, item.alias_global, "Expected a name after 'as'");
        } else {
            item.kind = IK::This;
            expect(K::ThisKeyword, "Expected 'this' after '@' in export");
        }

        return item;
    }

    item.kind = IK::Name;
    read_qualified(item.target_parts, item.target_global, is_import ? "Expected an imported name" : "Expected an exported name");

    if (is_import) {
        expect(K::FromKeyword, "Expected 'from' in import");
        item.has_source = true;
        item.source = read_string("Expected a module path string after 'from'");
    }

    if (match(K::AsKeyword)) {
        item.has_alias = true;
        read_qualified(item.alias_parts, item.alias_global, "Expected an alias name after 'as'");
    }

    if (item.is_template) {
        std::string_view local = (item.has_alias && !item.alias_parts.empty()) ? item.alias_parts.back()
                               : (!item.target_parts.empty())                  ? item.target_parts.back()
                               : std::string_view{};

        register_template_name(local);
    }

    return item;
}

nodes::ASTNode* Parser::parse_import_export(nodes::ImportExportDeclaration::Direction dir) {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    advance(); 
    const bool is_import = (dir == nodes::ImportExportDeclaration::Direction::Import);
    const bool block     = concrete_match(K::LeftCurly);
    auto* decl = make<nodes::ImportExportDeclaration>(dir, block, static_cast<std::uint32_t>(line));

    if (block) {
        expect(K::LeftCurly, "Expected '{' to start import/export block");

        while (!concrete_match(K::RightCurly) && !concrete_match(K::End)) {
            decl->add_item(parse_import_export_item(is_import));
        }

        expect(K::RightCurly, "Expected '}' to close import/export block");
    } else {
        decl->add_item(parse_import_export_item(is_import));
    }

    return decl;
}

} // namespace parsing
} // namespace walnut
