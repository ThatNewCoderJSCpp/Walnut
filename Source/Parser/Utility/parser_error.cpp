#include "Parser/Utility/parser_error.hpp"

#include <cctype>

namespace walnut {
namespace parsing {

namespace {

thread_local tokenizing::Token g_previous;
thread_local bool g_has_previous = false;

std::string_view punctuation_of(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    switch (k) {
        case K::LeftParen: return "(";            case K::RightParen: return ")";
        case K::LeftSquare: return "[";           case K::RightSquare: return "]";
        case K::LeftCurly: return "{";            case K::RightCurly: return "}";
        case K::LessThan: return "<";             case K::GreaterThan: return ">";
        case K::Equal: return "=";                case K::Plus: return "+";
        case K::Minus: return "-";                case K::Asterisk: return "*";
        case K::Slash: return "/";                case K::Hash: return "#";
        case K::Dot: return ".";                  case K::Comma: return ",";
        case K::Colon: return ":";                case K::Semicolon: return ";";
        case K::SingleQuote: return "'";          case K::DoubleQuote: return "\"";
        case K::Pipe: return "|";                 case K::Backtick: return "`";
        case K::Tilde: return "~";                case K::ExclamationMark: return "!";
        case K::QuestionMark: return "?";         case K::AtSymbol: return "@";
        case K::Caret: return "^";                case K::Percent: return "%";
        case K::BackSlash: return "\\";           case K::MoneySymbol: return "$";
        case K::Ampersand: return "&";            case K::Underscore: return "_";
        case K::DoublePlus: return "++";          case K::PlusEqual: return "+=";
        case K::DoublePlusEqual: return "++=";    case K::LogicEqual: return "==";
        case K::DoubleMinus: return "--";         case K::MinusEqual: return "-=";
        case K::DoubleMinusEqual: return "--=";   case K::AsteriskEqual: return "*=";
        case K::DoubleAsterisk: return "**";      case K::DoubleAsteriskEqual: return "**=";
        case K::SlashEqual: return "/=";          case K::PercentEqual: return "%=";
        case K::LessEqual: return "<=";           case K::GreaterEqual: return ">=";
        case K::NotEqual: return "!=";            case K::LogicOr: return "||";
        case K::LogicAnd: return "&&";            case K::SingleRightArrow: return "->";
        case K::SingleLeftArrow: return "<-";     case K::DoubleRightArrow: return "=>";
        case K::Ellipsis: return "...";           case K::DoubleColon: return "::";
        case K::DoubleSemicolon: return ";;";     case K::DoubleLessThan: return "<<";
        case K::DoubleGreaterThan: return ">>";   case K::CaretEqual: return "^=";
        case K::ColonEqual: return ":=";          case K::PipeEqual: return "|=";
        case K::AmpersandEqual: return "&=";      case K::ShiftLeftEqual: return "<<=";
        case K::ShiftRightEqual: return ">>=";
        default: return {};
    }
}

bool closes_statement(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    return k == K::Semicolon || k == K::DoubleSemicolon;
}

} // namespace

void ParserError::note_previous(const tokenizing::Token& token) noexcept {
    g_previous = token;
    g_has_previous = true;
}

void ParserError::forget_previous() noexcept { g_has_previous = false; }

std::string ParserError::spell(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    const std::string_view p = punctuation_of(k);
    if (!p.empty()) return "'" + std::string(p) + "'";
    tokenizing::Token probe(k, 0);
    if (probe.is_keyword() && probe.spelling_count() > 0) return "'" + std::string(probe.spellings()[0]) + "'";

    switch (k) {
        case K::Identifier:  return "a name";
        case K::Integer:     return "a whole number";
        case K::Float:       return "a number";
        case K::String:      return "a string literal";
        case K::Character:   return "a character literal";
        case K::TextLiteral: return "a text literal";
        case K::End:         return "the end of the file";
        case K::NewLine:     return "a line break";
        default:             return std::string(tokenizing::Token::name_fast(k));
    }
}

std::string ParserError::describe(const tokenizing::Token& token) {
    using K = tokenizing::Token::Kind;
    const std::string lexeme(token.lexeme());

    switch (token.kind()) {
        case K::Identifier:  return "the name '" + lexeme + "'";
        case K::Integer:
        case K::Float:       return "the number '" + lexeme + "'";
        case K::String:      return "the string " + (lexeme.empty() ? std::string("literal") : lexeme);
        case K::Character:   return "the character " + lexeme;
        case K::TextLiteral: return "a text literal";
        case K::End:         return "the end of the file";
        default: break;
    }

    if (token.is_keyword()) return "the keyword '" + (lexeme.empty() ? std::string(token.spelling_count() ? token.spellings()[0] : token.kind_name()) : lexeme) + "'";
    const std::string_view p = punctuation_of(token.kind());
    if (!p.empty()) return "'" + std::string(p) + "'";
    if (!lexeme.empty()) return "'" + lexeme + "'";
    return std::string(token.kind_name());
}

bool ParserError::keyword_names_function(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    switch (k) {
        case K::EndKeyword: case K::InKeyword: case K::IsKeyword: case K::AsKeyword: case K::FromKeyword:
        case K::ThenKeyword: case K::DownKeyword: case K::InheritsKeyword: case K::ExtendsKeyword:
        case K::ImplicitKeyword: case K::ExplicitKeyword: case K::OverrideKeyword: case K::AbstractKeyword:
        case K::FallthroughKeyword: case K::RepeatKeyword: case K::HoistKeyword: case K::HiddenKeyword:
        case K::LocalKeyword: case K::GlobalKeyword: case K::ExportKeyword: case K::IncludeKeyword:
        case K::ModuleKeyword: case K::MutableKeyword: case K::LazyKeyword: case K::FriendKeyword: case K::VirtualKeyword:
        case K::ConceptKeyword: case K::AsyncKeyword: case K::InfinityKeyword: case K::NanKeyword:
            return true;
        default:
            return false;
    }
}

std::string ParserError::keyword_as_name(const tokenizing::Token& token) {
    const std::string word(token.lexeme());
    std::string out = "'" + word + "' is a keyword, so it can't be used as a name here";
    const std::size_t n = token.spelling_count();

    if (n > 1) {
        const std::string primary(token.spellings()[0]);
        std::vector<std::string> rest;
        for (std::size_t i = 0; i < n; ++i) {
            const std::string s(token.spellings()[i]);
            if (s == word || s == primary) continue;
            rest.push_back("'" + s + "'");
        }
        std::string others;
        for (std::size_t i = 0; i < rest.size(); ++i) others += (i == 0 ? "" : (i + 1 == rest.size() ? " and " : ", ")) + rest[i];
        if (word != primary) out += " ('" + word + "' is another way to write '" + primary + "'" + (others.empty() ? std::string() : "; so are " + others) + ")";
        else if (!others.empty()) out += " (it can also be written " + others + ")";
    }

    out += ".\nPick a different name, such as '" + word + "_value' or 'my_" + word + "'";
    if (keyword_names_function(token.kind())) out += " ('" + word + "' can still be the name of a function or method)";
    return out;
}

std::string ParserError::format_token_list(const std::vector<tokenizing::Token::Kind>& expected_tokens) {
    std::stringstream ss;
    if (expected_tokens.empty()) return "";
    if (expected_tokens.size() == 1) { return spell(expected_tokens[0]); }
        
    for (std::size_t i = 0; i < expected_tokens.size(); ++i) {
        if (i > 0) {
            if (i == 1 && expected_tokens.size() == 2) {
                ss << " or ";
            } else if (i == expected_tokens.size() - 1) {
                ss << ", or ";
            } else {
                ss << ", ";
            }
        }
            
        ss << spell(expected_tokens[i]);
    }

    return ss.str();
}

std::string ParserError::format_code_context(const tokenizing::Token& token, const std::string& context) {
    if (context.empty()) return "";
    return "\nContext: " + context;
}

std::string ParserError::format_token_info(const tokenizing::Token& token) {
    std::string lexeme = std::string(token.lexeme());
    if (!lexeme.empty()) { return " '" + lexeme + "'"; }
    return "";
}

ParserError ParserError::make_and_report(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& message) {
    reporter.report_at(ErrorPhase::Parser, token.file_id(), token.line(), message, token.data(), token.length());
    return ParserError(message);
}

bool ParserError::previous_line_end(const tokenizing::Token& token, tokenizing::Token& out) {
    if (!g_has_previous || g_previous.line() == 0 || g_previous.line() >= token.line() || g_previous.file_id() != token.file_id()) return false;
    if (g_previous.kind() == tokenizing::Token::Kind::End) return false;
    out = g_previous;
    return true;
}

ParserError ParserError::missing_at_line_end(ErrorReporter& reporter, const tokenizing::Token& token, tokenizing::Token::Kind expected, const std::string& context) {
    tokenizing::Token prev;
    previous_line_end(token, prev);
    std::stringstream ss;
    ss << build_error_header(prev, "Syntax");
    ss << "Expected " << spell(expected) << " at the end of this line, after " << describe(prev);
    if (!context.empty()) ss << "\nContext: " << context;
    const std::string message = ss.str();
    reporter.report_at(ErrorPhase::Parser, prev.file_id(), prev.line(), message, prev.data() ? prev.data() + prev.length() : nullptr, 1);
    return ParserError(message);
}

ParserError ParserError::unexpected_token(ErrorReporter& reporter, const tokenizing::Token& token, const std::vector<tokenizing::Token::Kind>& expected_tokens, const std::string& context) {
    tokenizing::Token prev;
    if (expected_tokens.size() == 1 && closes_statement(expected_tokens[0]) && previous_line_end(token, prev)) return missing_at_line_end(reporter, token, expected_tokens[0], context);
    std::stringstream ss;
    ss << build_error_header(token, "Syntax");

    if (token.is_keyword() && std::find(expected_tokens.begin(), expected_tokens.end(), tokenizing::Token::Kind::Identifier) != expected_tokens.end()) {
        ss << keyword_as_name(token);
        ss << format_code_context(token, context);
        return make_and_report(reporter, token, ss.str());
    }

    ss << "Unexpected " << describe(token);
    if (!expected_tokens.empty()) { ss << "\nExpected: " << format_token_list(expected_tokens); }
    ss << format_code_context(token, context);
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::unexpected_token(
    ErrorReporter& reporter,
    const tokenizing::Token& token,
    std::string_view expected_description,
    const std::string& context
) {
    std::stringstream ss;
    ss << build_error_header(token, "Syntax");
    std::string lowered(expected_description);
    for (char& ch : lowered) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const bool wants_name = lowered.find("identifier") != std::string::npos || lowered.find("name") != std::string::npos;

    if (token.is_keyword() && wants_name) {
        ss << keyword_as_name(token);
        ss << format_code_context(token, context);
        return make_and_report(reporter, token, ss.str());
    }

    ss << "Unexpected " << describe(token);
    ss << "\nExpected: " << expected_description;
    ss << format_code_context(token, context);
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::invalid_expression(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& message) {
    std::stringstream ss;
    ss << build_error_header(token, "Expression");
    ss << message;
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::invalid_declaration(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& message) {
    std::stringstream ss;
    ss << build_error_header(token, "Declaration");
    ss << message;
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::missing_token(ErrorReporter& reporter, const tokenizing::Token& token, tokenizing::Token::Kind expected, const std::string& context) {
    tokenizing::Token prev;
    if (closes_statement(expected) && previous_line_end(token, prev)) return missing_at_line_end(reporter, token, expected, context);
    std::stringstream ss;
    ss << build_error_header(token, "Syntax");

    if (expected == tokenizing::Token::Kind::Identifier && token.is_keyword()) {
        ss << keyword_as_name(token);
        ss << format_code_context(token, context);
        return make_and_report(reporter, token, ss.str());
    }

    ss << "Expected " << spell(expected) << ", but found " << describe(token);
    ss << format_code_context(token, context);
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::invalid_operator(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& op_type) {
    std::stringstream ss;
    ss << build_error_header(token, "Operator");
    ss << "Invalid " << op_type << " operator" << format_token_info(token);
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::max_nesting_exceeded(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& construct_type) {
    std::stringstream ss;
    ss << build_error_header(token, "Nesting");
    ss << "Maximum nesting depth exceeded for " << construct_type;
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::dangling_operator(ErrorReporter& reporter, const tokenizing::Token& token) {
    std::stringstream ss;
    ss << build_error_header(token, "Syntax");
    ss << "Dangling operator" << format_token_info(token);
    ss << "\nOperators must have operands on both sides";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::invalid_literal(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& details) {
    std::stringstream ss;
    ss << build_error_header(token, "Literal");
    ss << "Invalid literal value" << format_token_info(token);
    if (!details.empty()) { ss << "\n" << details; }
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::mismatched_delimiters(ErrorReporter& reporter, const tokenizing::Token& token, char opening, char closing, const std::string& delimiter_type) {
    std::stringstream ss;
    ss << build_error_header(token, "Syntax");
    ss << "Mismatched " << delimiter_type << ": found '" << closing << "' but expected '" << opening << "'";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::invalid_identifier_format(ErrorReporter& reporter, const tokenizing::Token& token) {
    std::stringstream ss;
    ss << build_error_header(token, "Identifier");
    ss << "Invalid identifier format" << format_token_info(token);
    ss << "\nIdentifiers must:\n";
    ss << "- Start with a letter or underscore\n";
    ss << "- Contain only letters, numbers, and underscores\n";
    ss << "- Not be a reserved keyword";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::unexpected_end_of_input(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& context) {
    std::stringstream ss;
    ss << build_error_header(token, "Syntax");
    ss << "Unexpected end of input while parsing " << context;
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::invalid_escape_sequence(
    ErrorReporter& reporter, const tokenizing::Token& token,
    const std::string& why, std::size_t offset 
) {
    std::stringstream ss;
    ss << build_error_header(token, "String") << why;
    if (offset) ss << " (at offset " << offset << " in the literal)";
    ss << "\nSupported escapes: \\n \\t \\r \\a \\b \\f \\v \\e \\0 \\\\ \\' \\\" \\? \\` \\$"
          ", \\xNN, \\uXXXX, \\UXXXXXXXX, \\u{XXXXX} and \\s{name}";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::empty_block(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& block_type) {
    std::stringstream ss;
    ss << build_error_header(token, "Structure");
    ss << "Empty " << block_type << " block\n";
    ss << "Block must contain at least one statement";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::duplicate_type_modifiers(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& identifier_name) {
    std::stringstream ss;
    ss << build_error_header(token, "Modifier Duplication");
    ss << "'" << token.lexeme() << "' is written more than once";
    if (!identifier_name.empty()) { ss << " on '" << identifier_name << "'"; }
    ss << "; write each modifier once";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::duplicate_function_qualifiers(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& func_name) {
    std::stringstream ss;
    ss << build_error_header(token, "Function Qualifier Duplication");
    ss << "'" << token.lexeme() << "' is written more than once";
    if (!func_name.empty()) { ss << " on function '" << func_name << "'"; }
    ss << "; write each qualifier once";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::namespace_mismatch(
    ErrorReporter& reporter,
    const tokenizing::Token& token,
    const std::string& opening_name,
    const std::string& closing_name
) {
    std::stringstream ss;
    ss << build_error_header(token, "Namespace");
    ss << "This 'end namespace " << closing_name << "' closes namespace '" << opening_name << "'; write 'end namespace " << opening_name << "'";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::function_mismatch(
    ErrorReporter& reporter,
    const tokenizing::Token& token,
    const std::string& opening_name,
    const std::string& closing_name
) {
    std::stringstream ss;
    ss << build_error_header(token, "Function");
    ss << "This 'end function " << closing_name << "' closes function '" << opening_name << "'; write 'end function " << opening_name << "'";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::record_mismatch(
    ErrorReporter& reporter,
    const tokenizing::Token& token,
    const std::string& keyword,       
    const std::string& opening_name,
    const std::string& closing_name
) {
    std::stringstream ss;
    ss << build_error_header(token, "Type");
    ss << "This 'end " << keyword << " " << closing_name << "' closes " << keyword << " '" << opening_name << "'; write 'end " << keyword << " " << opening_name << "'";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::ambiguous_template(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& name) {
    std::stringstream ss;
    ss << build_error_header(token, "Syntax");
    ss << "Ambiguous '<' after '" << name << "'";
    ss << format_token_info(token);
    ss << "\nThis could be a template instantiation or a comparison, and '" << name << "' is not a known template.";
    ss << "\nDisambiguate with turbofish (e.g. '" << name << "::<...>') or parenthesize the comparison.";
    return make_and_report(reporter, token, ss.str());
}

ParserError ParserError::invalid_operator_overload(
    ErrorReporter& reporter,
    const tokenizing::Token& token,
    const std::vector<tokenizing::Token::Kind>& op_tokens
) {
    std::string spelling;

    for (std::size_t i = 0; i < op_tokens.size(); ++i) {
        if (i) spelling += ' ';
        const std::string one = spell(op_tokens[i]);
        spelling += one.size() >= 2 && one.front() == '\'' ? one.substr(1, one.size() - 2) : one;
    }
        
    std::stringstream ss;
    ss << build_error_header(token, "Operator");
    ss << "'" << spelling << "' is not an overloadable operator";
    return make_and_report(reporter, token, ss.str());
}

} // namespace parsing
} // namespace walnut
