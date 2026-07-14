//
// Scoped symbol table built from a Pascal AST (Milestone 2 Stage 4).
//

#ifndef APOLLO_PASCAL_SYMBOL_TABLE_HPP
#define APOLLO_PASCAL_SYMBOL_TABLE_HPP

#pragma once

#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceLocation.hpp"
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
};

class SymbolTable {
public:
    explicit SymbolTable(apollo::common::DiagnosticEngine &diagnostics) noexcept;

    void pushScope();
    void popScope();

    /// Declare in the current scope. Returns false and reports on duplicate.
    [[nodiscard]] bool declare(SymbolKind kind, std::string_view name,
                               apollo::common::SourceLocation location);

private:
    apollo::common::DiagnosticEngine *diagnostics_;
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
};

/// Seed console I/O builtins and collect declarations from the AST.
void buildSymbolTable(const ast::Program &program, apollo::common::DiagnosticEngine &diagnostics);

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_SYMBOL_TABLE_HPP
