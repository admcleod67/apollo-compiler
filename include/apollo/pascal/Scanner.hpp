//
// Pascal scanner: SourceFile -> TokenStream with diagnostics.
//

#ifndef APOLLO_PASCAL_SCANNER_HPP
#define APOLLO_PASCAL_SCANNER_HPP

#pragma once

#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/TokenStream.hpp"

namespace apollo::pascal {

/// Lex an entire compilation unit. Always returns a stream ending in EndOfFile.
/// Ordinary lexical errors are reported through diagnostics (no throw).
[[nodiscard]] TokenStream scan(const apollo::common::SourceFile &source,
                               apollo::common::DiagnosticEngine &diagnostics);

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_SCANNER_HPP
