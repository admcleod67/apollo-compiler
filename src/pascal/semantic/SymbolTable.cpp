#include "apollo/pascal/SymbolTable.hpp"

#include "apollo/common/Diagnostic.hpp"
#include "apollo/pascal/Analyse.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace apollo::pascal {
namespace {

char toLowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

std::string foldAsciiLower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(toLowerAscii(c));
    }
    return out;
}

} // namespace

SymbolTable::SymbolTable(apollo::common::DiagnosticEngine &diagnostics) noexcept
    : diagnostics_(&diagnostics) {
    pushScope();
}

void SymbolTable::pushScope() {
    scopes_.emplace_back();
}

void SymbolTable::popScope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

bool SymbolTable::declare(SymbolKind kind, std::string_view name,
                          apollo::common::SourceLocation location, TypePtr type,
                          bool isVarParam) {
    if (scopes_.empty()) {
        pushScope();
    }

    const std::string key = foldAsciiLower(name);
    auto &scope = scopes_.back();
    if (scope.find(key) != scope.end()) {
        diagnostics_->report(apollo::common::DiagnosticSeverity::Error, location,
                             "duplicate declaration of '" + std::string(name) + "'");
        return false;
    }

    Symbol symbol;
    symbol.kind = kind;
    symbol.name = std::string(name);
    symbol.location = location;
    symbol.type = std::move(type);
    symbol.isVarParam = isVarParam;
    scope.emplace(key, std::move(symbol));
    return true;
}

const Symbol *SymbolTable::lookup(std::string_view name) const {
    const std::string key = foldAsciiLower(name);
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(key);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

SymbolTable buildSymbolTable(ast::Program &program,
                             apollo::common::DiagnosticEngine &diagnostics) {
    return analyse(program, diagnostics);
}

} // namespace apollo::pascal
