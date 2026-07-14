//
// Minimal Pascal AST for Milestone 2 Stage 1 (program / block / compound).
//

#ifndef APOLLO_PASCAL_AST_AST_HPP
#define APOLLO_PASCAL_AST_AST_HPP

#pragma once

#include "apollo/common/SourceLocation.hpp"

#include <string>
#include <vector>

namespace apollo::pascal::ast {

/// Placeholder for Stage 2+ statement nodes. Stage 1 leaves the vector empty.
struct Stmt {
    apollo::common::SourceRange range{};
};

struct CompoundStmt {
    apollo::common::SourceRange range{};
    std::vector<Stmt> statements;
};

struct Block {
    apollo::common::SourceRange range{};
    CompoundStmt body;
};

struct Program {
    apollo::common::SourceRange range{};
    std::string name;
    Block block;
};

} // namespace apollo::pascal::ast

#endif // APOLLO_PASCAL_AST_AST_HPP
