//
// Pascal semantic analysis (use-resolution & expression typing).
//

#ifndef APOLLO_PASCAL_ANALYSE_HPP
#define APOLLO_PASCAL_ANALYSE_HPP

#pragma once

#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/pascal/SymbolTable.hpp"
#include "apollo/pascal/ast/Ast.hpp"

namespace apollo::pascal {

/// Seed symbols, declare bindings, and type every expression in statement bodies.
[[nodiscard]] SymbolTable analyse(ast::Program &program,
                                  apollo::common::DiagnosticEngine &diagnostics);

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_ANALYSE_HPP
