#include "apollo/common/DiagnosticEngine.hpp"

#include <ostream>
#include <utility>

namespace apollo::common {
namespace {

const char *severityText(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Note:
        return "note";
    }
    return "error";
}

} // namespace

DiagnosticEngine::DiagnosticEngine(const SourceFile &source) noexcept : source_(&source) {}

void DiagnosticEngine::report(DiagnosticSeverity severity, SourceLocation location,
                              std::string message) {
    diagnostics_.push_back(Diagnostic{severity, std::move(message), location});
}

std::size_t DiagnosticEngine::errorCount() const noexcept {
    std::size_t count = 0;
    for (const auto &diagnostic : diagnostics_) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            ++count;
        }
    }
    return count;
}

void DiagnosticEngine::write(std::ostream &out) const {
    const std::string &path = source_->path();
    for (const auto &diagnostic : diagnostics_) {
        out << path << ':' << diagnostic.location.line << ':' << diagnostic.location.column << ": "
            << severityText(diagnostic.severity) << ": " << diagnostic.message << '\n';
    }
}

} // namespace apollo::common
