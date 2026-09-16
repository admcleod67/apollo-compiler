//
// Pascal scanner: SourceFile -> TokenStream with diagnostics.
//

#ifndef APOLLO_PASCAL_SCANNER_HPP
#define APOLLO_PASCAL_SCANNER_HPP

#pragma once

#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/TokenStream.hpp"

#include <memory>
#include <vector>

namespace apollo::pascal {

/// Owned source buffers (index 0 = root) plus the spliced token stream.
/// Lexemes are views into `sources`; keep this result alive while using tokens.
struct ScanResult {
    std::vector<std::unique_ptr<apollo::common::SourceFile>> sources;
    TokenStream tokens;
};

/// Lex an entire compilation unit, expanding `{$I}` / `{$i}` includes.
/// Always returns a stream ending in EndOfFile. Lexical errors are reported
/// through diagnostics (no throw).
[[nodiscard]] ScanResult scan(const apollo::common::SourceFile &source,
                              apollo::common::DiagnosticEngine &diagnostics);

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_SCANNER_HPP
