//
// Diagnostic collector and renderer for a single SourceFile.
//

#ifndef APOLLO_COMMON_DIAGNOSTIC_ENGINE_HPP
#define APOLLO_COMMON_DIAGNOSTIC_ENGINE_HPP

#pragma once

#include "apollo/common/Diagnostic.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/common/SourceLocation.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace apollo::common {

/// Collects diagnostics for a compilation (possibly multi-file via includes).
/// Does not own the primary SourceFile.
class DiagnosticEngine {
public:
    explicit DiagnosticEngine(const SourceFile &source) noexcept;

    /// Path used for diagnostics that omit an explicit path (initially `source.path()`).
    void setActivePath(std::string_view path);
    [[nodiscard]] const std::string &activePath() const noexcept { return activePath_; }

    void report(DiagnosticSeverity severity, SourceLocation location, std::string message);

    [[nodiscard]] std::size_t errorCount() const noexcept;
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept { return diagnostics_; }

    /// Write diagnostics as `path:line:col: severity: message` (one per line).
    void write(std::ostream &out) const;

private:
    const SourceFile *source_;
    std::string activePath_;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace apollo::common

#endif // APOLLO_COMMON_DIAGNOSTIC_ENGINE_HPP
