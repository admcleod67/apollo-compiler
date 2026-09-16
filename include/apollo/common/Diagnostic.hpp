//
// Compiler diagnostic records.
//

#ifndef APOLLO_COMMON_DIAGNOSTIC_HPP
#define APOLLO_COMMON_DIAGNOSTIC_HPP

#pragma once

#include "apollo/common/SourceLocation.hpp"

#include <string>

namespace apollo::common {

enum class DiagnosticSeverity {
    Error,
    Warning,
    Note,
};

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::string message;
    SourceLocation location{};
    /// Path captured at report time; empty means use the engine's primary source path.
    std::string path;
};

} // namespace apollo::common

#endif // APOLLO_COMMON_DIAGNOSTIC_HPP
