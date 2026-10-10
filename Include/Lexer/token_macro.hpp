#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <string_view>
#include <cstdint>
#include <ostream>

namespace walnut {
namespace tokenizing {

#define TOKEN_LIST(X) \
    /* Literals */ \
    X(Float, false, "") X(Integer, false, "") X(Identifier, false, "") \
    /* Delimiters */ \
    X(LeftParen, false, "") X(RightParen, false, "") \
    X(LeftSquare, false, "") X(RightSquare, false, "") \
    X(LeftCurly, false, "") X(RightCurly, false, "") \
    /* Comparisons */ \
    X(LessThan, false, "") X(GreaterThan, false, "") X(Equal, false, "") \
    /* Arithmetic */ \
    X(Plus, false, "") X(Minus, false, "") X(Asterisk, false, "") X(Slash, false, "") \
    /* Punctuation */ \
    X(Hash, false, "") X(Dot, false, "") X(Comma, false, "") X(Colon, false, "") X(Semicolon, false, "") \
    X(SingleQuote, false, "") X(DoubleQuote, false, "") \
    /* Comments */ \
    X(Comment, false, "") X(LongComment, false, "") \
    /* Misc */ \
    X(Pipe, false, "") X(End, false, "") X(Unexpected, false, "") X(NewLine, false, "") X(Backtick, false, "") X(Tilde, false, "") \
    X(String, false, "") X(ExclamationMark, false, "") X(QuestionMark, false, "") X(AtSymbol, false, "") \
    X(Caret, false, "") X(Percent, false, "") X(BackSlash, false, "") X(MoneySymbol, false, "") X(Ampersand, false, "") X(Underscore, false, "") \
    /* Compound operators */ \
    X(DoublePlus, false, "") X(PlusEqual, false, "") X(DoublePlusEqual, false, "") X(LogicEqual, false, "") \
    X(DoubleMinus, false, "") X(MinusEqual, false, "") X(DoubleMinusEqual, false, "") \
    X(AsteriskEqual, false, "") X(DoubleAsterisk, false, "") X(DoubleAsteriskEqual, false, "") \
    X(SlashEqual, false, "") X(PercentEqual, false, "") X(LessEqual, false, "") X(GreaterEqual, false, "") X(NotEqual, false, "") \
    X(LogicOr, false, "") X(LogicAnd, false, "") \
    X(SingleRightArrow, false, "") X(SingleLeftArrow, false, "") X(DoubleRightArrow, false, "") \
    X(Ellipsis, false, "") X(Character, false, "") X(TextLiteral, false, "") \
    X(DoubleColon, false, "") X(DoubleSemicolon, false, "") \
    X(DoubleLessThan, false, "") X(DoubleGreaterThan, false, "") \
    X(CaretEqual, false, "") X(ColonEqual, false, "") X(PipeEqual, false, "") X(AmpersandEqual, false, "") \
    X(ShiftLeftEqual, false, "") X(ShiftRightEqual, false, "") \
    /* Keywords */ \
    X(ArrayKeyword, true, "array", "arr") \
    X(AsyncKeyword, true, "async") \
    X(AsKeyword, true, "as") \
    X(AwaitKeyword, true, "await") \
    X(AlignofKeyword, true, "alignof", "align_of") \
    X(AlignasKeyword, true, "alignas", "align_as") \
    X(BitCastKeyword, true, "bitcast", "bit_cast") \
    X(BreakKeyword, true, "break") \
    X(CaseKeyword, true, "case") \
    X(ClassKeyword, true, "class") \
    X(ContinueKeyword, true, "continue") \
    X(CatchKeyword, true, "catch") \
    X(ConceptKeyword, true, "concept") \
    X(ConstexprKeyword, true, "constexpr") \
    X(ConstevalKeyword, true, "consteval") \
    X(ConstinitKeyword, true, "constinit") \
    X(ConstructorKeyword, true, "constructor") \
    X(ConstCastKeyword, true, "const_cast") \
    X(CountOfKeyword, true, "countof", "count_of") \
    X(CoAwaitKeyword,  true, "co_await")  \
    X(CoReturnKeyword, true, "co_return") \
    X(CoYieldKeyword,  true, "co_yield")  \
    X(DoKeyword, true, "do") \
    X(DefaultKeyword, true, "default") \
    X(DeleteKeyword, true, "delete") \
    X(DecltypeKeyword, true, "decltype") \
    X(DownKeyword, true, "down") \
    X(DestructorKeyword, true, "destructor") \
    X(DynamicKeyword, true, "dynamic") \
    X(DynamicCastKeyword, true, "dynamic_cast") \
    X(DiscardConstKeyword, true, "discard_const") \
    X(DiscardNodiscardKeyword, true, "discard_nodiscard", "discard_no_discard") \
    X(ElseKeyword, true, "else") \
    X(EndKeyword, true, "end") \
    X(EnumKeyword, true, "enum") \
    X(ExportKeyword, true, "export") \
    X(ExtendsKeyword, true, "extends") \
    X(ExternKeyword, true, "extern") \
    X(ExplicitKeyword, true, "explicit") \
    X(False, true, "false", "FALSE", "False") \
    X(FallthroughKeyword, true, "fallthrough") \
    X(ForKeyword, true, "for") \
    X(FunctionKeyword, true, "function") \
    X(FriendKeyword, true, "friend") \
    X(FromKeyword, true, "from") \
    X(GlobalKeyword, true, "global") \
    X(HoistKeyword, true, "hoist", "hoisted") \
    X(HiddenKeyword, true, "hidden") \
    X(IfKeyword, true, "if") \
    X(ImportKeyword, true, "import") \
    X(IncludeKeyword, true, "include") \
    X(InKeyword, true, "in") \
    X(InlineKeyword, true, "inline") \
    X(InfinityKeyword, true, "infinity", "inf") \
    X(ImmutableKeyword, true, "immutable") \
    X(ImplicitKeyword, true, "implicit") \
    X(InheritsKeyword, true, "inherits", "inherit") \
    X(IsKeyword, true, "is") \
    X(LocalKeyword, true, "local") \
    X(MutableKeyword, true, "mutable") \
    X(ModuleKeyword, true, "module") \
    X(NamespaceKeyword, true, "namespace") \
    X(NewKeyword, true, "new") \
    X(NoexceptKeyword, true, "noexcept") \
    X(NullptrKeyword, true, "nullptr") \
    X(NodiscardKeyword, true, "nodiscard", "no_discard") \
    X(OperatorKeyword, true, "operator") \
    X(OverrideKeyword, true, "override") \
    X(OverloadKeyword, true, "overload") \
    X(PrivateKeyword, true, "private") \
    X(PublicKeyword, true, "public") \
    X(ProtectedKeyword, true, "protected") \
    X(RepeatKeyword, true, "repeat") \
    X(ReinterpretCastKeyword, true, "reinterpret_cast") \
    X(ReturnKeyword, true, "return") \
    X(RequiresKeyword, true, "requires") \
    X(StaticKeyword, true, "static") \
    X(StaticCastKeyword, true, "static_cast") \
    X(StaticAssertKeyword, true, "static_assert") \
    X(SizeofKeyword, true, "sizeof", "size_of") \
    X(StructKeyword, true, "struct") \
    X(SwitchKeyword, true, "switch") \
    X(ThenKeyword, true, "then") \
    X(ThisKeyword, true, "this") \
    X(ThrowKeyword, true, "throw") \
    X(TryKeyword, true, "try") \
    X(True, true, "true", "TRUE", "True") \
    X(TypedefKeyword, true, "typedef") \
    X(TypeofKeyword, true, "typeof", "type_of") \
    X(ThreadLocalKeyword, true, "threadlocal", "thread_local") \
    X(TemplateKeyword, true, "template") \
    X(TypenameKeyword, true, "typename") \
    X(TypeidKeyword, true, "typeid", "type_id") \
    X(UnionKeyword, true, "union") \
    X(UsingKeyword, true, "using") \
    X(VariantKeyword, true, "variant") \
    X(VirtualKeyword, true, "virtual") \
    X(VoidKeyword, true, "void") \
    X(VolatileKeyword, true, "volatile") \
    X(WhileKeyword, true, "while") \
    /* Type declarations */ \
    X(AutoDeclaration, true, "auto", "automatic") \
    X(DynamicDeclaration, true, "var", "variable", "let", "def", "define", "any") \
    X(BooleanDeclaration, true, "bool", "boolean") \
    X(CharacterDeclaration, true, "char", "character") \
    X(ConstantDeclaration, true, "const", "constant") \
    X(DecimalDeclaration, true, "decimal", "dec", "float", "floater", "double") \
    X(IntegerDeclaration, true, "int", "integer") \
    X(LongDeclaration, true, "long") \
    X(StringDeclaration, true, "string", "str") \
    X(ShortDeclaration, true, "short") \
    X(UnsignedDeclaration, true, "unsigned") \
    X(TextDeclaration, true, "text", "txt")

class Token {
public:
    enum class Kind : std::uint16_t {
        #define X(name, is_kw, ...) name,
        TOKEN_LIST(X)
        #undef X
        COUNT_
    };

    static constexpr std::string_view name(Kind k) noexcept {
        switch (k) {
            #define X(name, is_kw, ...) case Kind::name: return #name;
            TOKEN_LIST(X)
            #undef X
            default: return "Unknown";
        }
    }

    static constexpr std::string_view names[] = {
        #define X(name, is_kw, ...) #name,
        TOKEN_LIST(X)
        #undef X
    };

    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<std::size_t>(Kind::COUNT_));

    static constexpr std::string_view name_fast(Kind k) noexcept {
        std::size_t idx = static_cast<std::size_t>(k);
        return idx < static_cast<std::size_t>(Kind::COUNT_) ? names[idx] : "Unknown";
    }

    template <Kind K>
    struct KeywordEntry {
        static constexpr bool is_keyword = false;
        static constexpr std::string_view spellings[1] = {};
        static constexpr std::size_t count = 0;
    };

    struct KeywordInfo {
        bool is_keyword;
        const std::string_view* spellings;
        std::size_t spelling_count;
    };

    static constexpr KeywordInfo const* keyword_table_ptr() noexcept;

private:
    const char*   m_ptr  {nullptr};
    std::uint32_t m_len  {0};
    std::uint32_t m_line {1};
    std::uint32_t m_file = std::uint32_t(-1);
    Kind          m_kind {Kind::Unexpected};

public:
    constexpr Token() noexcept = default;
    constexpr Token(const Token&) noexcept = default;
    constexpr Token(Token&&) noexcept = default;
    constexpr Token& operator=(const Token&) noexcept = default;
    constexpr Token& operator=(Token&&) noexcept = default;

    constexpr Token(Kind kind, std::uint32_t line) noexcept : m_kind{kind}, m_line{line} {}
    constexpr Token(Kind kind, const char* beg, std::size_t len, std::uint32_t line) noexcept : m_kind{kind}, m_ptr{beg}, m_len{static_cast<std::uint32_t>(len)}, m_line{line} {}
    constexpr Token(Kind kind, const char* beg, const char* end, std::uint32_t line) noexcept : m_kind{kind}, m_ptr{beg}, m_len{static_cast<std::uint32_t>(end - beg)}, m_line{line} {}

public:
    constexpr Kind kind() const noexcept { return m_kind; }
    constexpr void kind(Kind k) noexcept { m_kind = k; }

    constexpr const char* data() const noexcept { return m_ptr; }
    constexpr std::uint32_t length() const noexcept { return m_len; }
    constexpr std::string_view lexeme() const noexcept { return std::string_view(m_ptr, m_len); }

    constexpr void lexeme(std::string_view lex) noexcept {
        m_ptr = lex.data();
        m_len = static_cast<std::uint32_t>(lex.size());
    }

    constexpr std::uint32_t line() const noexcept { return m_line; }

    constexpr std::uint32_t file_id() const noexcept { return m_file; }
    constexpr void file_id(std::uint32_t f) noexcept { m_file = f; }

public:
    constexpr bool is(Kind k) const noexcept { return m_kind == k; }
    constexpr bool is_not(Kind k) const noexcept { return m_kind != k; }

    template <typename... Kinds>
    constexpr bool is_one_of(Kind first, Kinds... rest) const noexcept {
        if constexpr (sizeof...(rest) == 0) {
            return is(first);
        } else {
            return is(first) || is_one_of(rest...);
        }
    }

    constexpr std::string_view kind_name() const noexcept { return name_fast(m_kind); }
    constexpr bool is_keyword() const noexcept { return keyword_table_ptr()[static_cast<std::size_t>(m_kind)].is_keyword; }
    constexpr const std::string_view* spellings() const noexcept { return keyword_table_ptr()[static_cast<std::size_t>(m_kind)].spellings; }
    constexpr std::size_t spelling_count() const noexcept { return keyword_table_ptr()[static_cast<std::size_t>(m_kind)].spelling_count; }
};

inline std::ostream& operator<<(std::ostream& os, Token::Kind k) { return os << Token::name(k); }

inline std::ostream& operator<<(std::ostream& os, const Token& t) {
    os << "Token(" << t.kind() << ", \"" << t.lexeme() << "\", line " << t.line() << ")";
    return os;
}

#define X(name, is_kw, ...) \
template <> \
struct Token::KeywordEntry<Token::Kind::name> { \
    static constexpr bool is_keyword = is_kw; \
    static constexpr std::string_view spellings[] = { __VA_ARGS__ }; \
    static constexpr std::size_t count = sizeof(spellings) / sizeof(spellings[0]); \
};
TOKEN_LIST(X)
#undef X

static constexpr Token::KeywordInfo Token_keyword_table[] = {
    #define X(name, is_kw, ...) \
        Token::KeywordInfo{ \
            Token::KeywordEntry<Token::Kind::name>::is_keyword, \
            Token::KeywordEntry<Token::Kind::name>::spellings, \
            Token::KeywordEntry<Token::Kind::name>::count \
        },
    TOKEN_LIST(X)
    #undef X
};

constexpr const Token::KeywordInfo* Token::keyword_table_ptr() noexcept { return Token_keyword_table; }

} // namespace tokenizing
} // namespace walnut

#endif // TOKEN_HPP