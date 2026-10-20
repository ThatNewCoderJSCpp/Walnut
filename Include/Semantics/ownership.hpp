#ifndef WALNUT_SEMA_OWNERSHIP_HPP
#define WALNUT_SEMA_OWNERSHIP_HPP

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "symbol.hpp"

#include "../Parser/nodes.hpp"
#include "../Common/error_reporter.hpp"
#include "../Common/compiler_warning.hpp"

namespace walnut {
namespace semantics {

class OwnershipChecker {
public:
    OwnershipChecker(ErrorReporter& reporter, WarningReporter& warnings) noexcept : m_reporter(reporter), m_warnings(warnings) {}

    void run(FileId file, nodes::ASTNode* root);

    std::size_t errors_found() const noexcept { return m_errors; }
    std::size_t warnings_found() const noexcept { return m_warned; }

private:
    enum class Life : std::uint8_t { Live = 0, Moved, Deleted, Dangling, Unknown };
    enum class Use : std::uint8_t { Read = 0, Deref, Escape, Address };

    struct Held {
        std::string   path;
        Symbol*       target   = nullptr;
        std::uint32_t line     = 0;
        bool          dead     = false;
        std::uint32_t declared = 0;
        bool          reported = false;
    };

    struct Var {
        Life          life       = Life::Live;
        std::uint32_t at         = 0;
        bool          owns_new   = false;
        std::uint32_t new_line   = 0;
        Symbol*       points_to  = nullptr;
        std::size_t   depth      = 0;
        bool          stack      = false;
        bool          array      = false;
        std::uint32_t decl_line  = 0;
        bool          reported   = false;
        bool          pointer    = false;
        bool          reference  = false;
        std::uint32_t heap       = 0;
        Symbol*       deleted_via = nullptr;
        std::vector<Held> held;
    };

    struct Flow {
        std::unordered_map<Symbol*, Var> vars;
        bool live = true;
    };

    struct Jump {
        bool              is_switch = false;
        std::vector<Flow> breaks;
        std::vector<Flow> continues;
    };

    ErrorReporter&   m_reporter;
    WarningReporter& m_warnings;
    FileId           m_file = INVALID_FILE;
    Flow             m_flow;
    std::vector<std::vector<Symbol*>> m_blocks;
    std::vector<Jump> m_jumps;
    bool             m_returns_ref = false;
    bool             m_returns_ptr = false;
    bool             m_gave_up = false;
    std::uint32_t    m_next_heap = 0;
    std::set<std::tuple<FileId, std::uint32_t, std::string>> m_seen;
    std::size_t      m_errors = 0;
    std::size_t      m_warned = 0;

    void find(nodes::ASTNode* n);
    void check_function(nodes::FunctionParameters* params, nodes::ASTNode* body, const parser_types::TypeInfo* ret);

    void statement(nodes::ASTNode* s);
    void block(nodes::BlockStatement* b);
    void open_block();
    void close_block();
    void declare(Symbol* sym, bool stack, bool array, std::uint32_t line);
    void declaration(nodes::VariableDeclaration* d);
    void if_statement(nodes::IfStatement* s);
    void loop(nodes::ASTNode* before, nodes::ASTNode* body, nodes::ASTNode* after, bool body_first);
    void switch_statement(nodes::SwitchStatement* s);
    void try_statement(nodes::TryCatchStatement* t);
    void return_statement(nodes::ReturnStatement* r);
    void jump(nodes::SingleStatement* s);

    void expr(nodes::ASTNode* e, Use use);
    void touch(Symbol* sym, Use use, std::uint32_t line);
    void assign(nodes::ASTNode* left, nodes::ASTNode* right, std::uint32_t line);
    void initialize(Symbol* sym, nodes::ASTNode* right, std::uint32_t line, bool reassign);
    void delete_expression(nodes::DeleteExpression* d);
    void escape_all(nodes::ASTNode* n);
    void leaks_at_exit(Symbol* keep);

    Var* var(Symbol* sym);
    std::uint32_t heap_of(Symbol* sym);
    Symbol* pointer_source(nodes::ASTNode* e);
    Symbol* lvalue_source(nodes::ASTNode* e);
    Symbol* local_value_base(nodes::ASTNode* e);
    void set_kind(Symbol* sym, const parser_types::TypeInfo& type);
    Symbol* holder_root(nodes::ASTNode* e, std::string& path, bool& local);
    Symbol* stored_target(nodes::ASTNode* e);
    void store(nodes::ASTNode* place, nodes::ASTNode* value, bool into_container, std::uint32_t line);
    void forget_stored(nodes::ASTNode* place, bool container);
    void check_held(nodes::ASTNode* place, std::uint32_t line);
    static std::string place_text(nodes::ASTNode* e);
    static bool stores_argument(std::string_view method);
    static Symbol* symbol_of(nodes::ASTNode* n);
    static bool is_move(nodes::CallExpression* c);
    static Flow join(const std::vector<Flow>& flows);

    void error(std::uint32_t line, std::string message);
    void warn(std::uint32_t line, std::string message);
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_OWNERSHIP_HPP
