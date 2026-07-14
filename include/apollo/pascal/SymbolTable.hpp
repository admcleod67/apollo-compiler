//
// Scoped typed symbol table (Milestone 3 Stage 1).
//

#ifndef APOLLO_PASCAL_SYMBOL_TABLE_HPP
#define APOLLO_PASCAL_SYMBOL_TABLE_HPP

#pragma once

#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceLocation.hpp"
#include "apollo/pascal/Type.hpp"
#include "apollo/pascal/ast/Ast.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace apollo::pascal {

enum class SymbolKind {
    Program,
    Const,
    Type,
    Var,
    Param,
    Procedure,
    Function,
    Builtin,
};

struct Symbol {
    SymbolKind kind{SymbolKind::Var};
    std::string name;
    apollo::common::SourceLocation location{};
    TypePtr type;
    bool isVarParam{false};
};

class SymbolTable {
public:
    explicit SymbolTable(apollo::common::DiagnosticEngine &diagnostics) noexcept;

    void pushScope();
    void popScope();

    /// Declare in the current scope. Returns false and reports on duplicate.
    [[nodiscard]] bool declare(SymbolKind kind, std::string_view name,
                               apollo::common::SourceLocation location, TypePtr type = {},
                               bool isVarParam = false);

    /// Look up a name from innermost scope outward. Null if not found.
    [[nodiscard]] const Symbol *lookup(std::string_view name) const;

private:
    apollo::common::DiagnosticEngine *diagnostics_;
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
};

/// Seed predefined types and I/O builtins, then collect typed declarations.
[[nodiscard]] SymbolTable buildSymbolTable(ast::Program &program,
                                           apollo::common::DiagnosticEngine &diagnostics);

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_SYMBOL_TABLE_HPP
