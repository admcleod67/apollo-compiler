//
// Pascal AST/symbol-table -> shared IR lowering (Milestone 4 Stage 2).
//

#ifndef APOLLO_PASCAL_IR_LOWER_HPP
#define APOLLO_PASCAL_IR_LOWER_HPP

#pragma once

#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/ir/Ir.hpp"
#include "apollo/pascal/SymbolTable.hpp"
#include "apollo/pascal/ast/Ast.hpp"

namespace apollo::pascal::ir {

/// Lower a semantically-checked straight-line program into a single-function IR module.
///
/// Only the top-level program body is lowered in Stage 2: `if`/`while`/`repeat`/`for` and
/// user subprogram bodies are not yet supported and are skipped with a diagnostic if
/// encountered (Stage 3). Callers should typically run this only on `--check`-clean input.
[[nodiscard]] apollo::ir::Module lowerToIr(const ast::Program &program,
                                           const SymbolTable &symbols,
                                           apollo::common::DiagnosticEngine &diagnostics);

} // namespace apollo::pascal::ir

#endif // APOLLO_PASCAL_IR_LOWER_HPP
