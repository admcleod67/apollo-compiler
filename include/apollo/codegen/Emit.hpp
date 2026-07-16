//
// Emit Gemini `.tbc` text from Apollo IR (Milestone 5 Stage 2: straight-line main).
//

#ifndef APOLLO_CODEGEN_EMIT_HPP
#define APOLLO_CODEGEN_EMIT_HPP

#pragma once

#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/ir/Ir.hpp"

#include <iosfwd>
#include <string>

namespace apollo::codegen {

/// Emit Gemini `.tbc` for a straight-line `main` IR module (single block, no branches).
/// On unsupported shapes/ops, reports diagnostics and returns an empty string.
[[nodiscard]] std::string emitTbc(const apollo::ir::Module &module,
                                  apollo::common::DiagnosticEngine &diagnostics);

void writeTbc(std::ostream &out, const apollo::ir::Module &module,
              apollo::common::DiagnosticEngine &diagnostics);

} // namespace apollo::codegen

#endif // APOLLO_CODEGEN_EMIT_HPP
