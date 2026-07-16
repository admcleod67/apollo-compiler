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

/// Lower a semantically-checked program into a multi-function IR module.
///
/// The program body lowers to a `main` function; `if`/`while`/`repeat`/`for` lower to
/// multi-block CFGs. Subprograms declared directly in the program block (one level) each
/// lower to their own flat IR function. Deeper subprogram nesting and access to enclosing
/// (non-own) scope locals are not yet supported and are diagnosed rather than guessed.
/// Callers should typically run this only on `--check`-clean input.
[[nodiscard]] apollo::ir::Module lowerToIr(const ast::Program &program,
                                           const SymbolTable &symbols,
                                           apollo::common::DiagnosticEngine &diagnostics);

} // namespace apollo::pascal::ir

#endif // APOLLO_PASCAL_IR_LOWER_HPP
