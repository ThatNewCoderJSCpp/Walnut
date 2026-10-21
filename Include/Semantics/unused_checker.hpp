#ifndef WALNUT_SEMA_UNUSED_CHECKER_HPP
#define WALNUT_SEMA_UNUSED_CHECKER_HPP

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "symbol.hpp"

#include "../Parser/nodes.hpp"
#include "../Common/compiler_warning.hpp"

namespace walnut {
namespace semantics {

class UnusedVariableChecker {
public:
    explicit UnusedVariableChecker(WarningReporter& warnings) noexcept : m_warnings(warnings) {}

    void run(FileId file, nodes::ASTNode* root);

    std::size_t warnings_found() const noexcept { return m_warned; }

private:
    struct Declared {
        std::vector<Symbol*>           symbols;
        std::vector<std::string_view>  names;
        std::uint32_t                  line = 0;
    };

    void find(nodes::ASTNode* n);
    void check_body(nodes::ASTNode* body);
    void collect(nodes::ASTNode* n, bool in_record);

    WarningReporter&              m_warnings;
    FileId                        m_file = INVALID_FILE;
    std::vector<Declared>         m_declared;
    std::set<const Symbol*>       m_used;
    std::set<std::string_view>    m_unresolved;
    std::size_t                   m_warned = 0;
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_UNUSED_CHECKER_HPP
