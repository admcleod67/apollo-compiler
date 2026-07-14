//
// Pascal recursive-descent parser: TokenStream -> AST.
//

#ifndef APOLLO_PASCAL_PARSER_HPP
#define APOLLO_PASCAL_PARSER_HPP

#pragma once

#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/TokenStream.hpp"
#include "apollo/pascal/ast/Ast.hpp"

#include <memory>

namespace apollo::pascal {

/// Parse a scanned compilation unit. Does not rescan source text; locations come from tokens.
/// Returns nullptr if a Program root cannot be formed. Errors are reported through diagnostics.
[[nodiscard]] std::unique_ptr<ast::Program> parse(const apollo::common::SourceFile &source,
                                                  const TokenStream &tokens,
                                                  apollo::common::DiagnosticEngine &diagnostics);

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_PARSER_HPP
