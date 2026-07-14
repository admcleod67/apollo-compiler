#include "apollo/pascal/SymbolTable.hpp"

#include "apollo/common/Diagnostic.hpp"

#include <string>
#include <string_view>

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

void walkBlock(SymbolTable &table, const ast::Block &block);

void walkSubprogram(SymbolTable &table, const ast::Subprogram &sub) {
    const SymbolKind kind = sub.isFunction ? SymbolKind::Function : SymbolKind::Procedure;
    (void)table.declare(kind, sub.name, sub.range.begin);

    table.pushScope();
    for (const auto &param : sub.params) {
        for (const auto &name : param.names) {
            (void)table.declare(SymbolKind::Param, name, param.range.begin);
        }
    }
    if (sub.block) {
        walkBlock(table, *sub.block);
    }
    table.popScope();
}

void walkBlock(SymbolTable &table, const ast::Block &block) {
    for (const auto &decl : block.consts) {
        (void)table.declare(SymbolKind::Const, decl.name, decl.range.begin);
    }
    for (const auto &decl : block.types) {
        (void)table.declare(SymbolKind::Type, decl.name, decl.range.begin);
    }
    for (const auto &decl : block.vars) {
        for (const auto &name : decl.names) {
            (void)table.declare(SymbolKind::Var, name, decl.range.begin);
        }
    }
    for (const auto &sub : block.subprograms) {
        walkSubprogram(table, sub);
    }
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
                          apollo::common::SourceLocation location) {
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
    scope.emplace(key, std::move(symbol));
    return true;
}

void buildSymbolTable(const ast::Program &program, apollo::common::DiagnosticEngine &diagnostics) {
    SymbolTable table(diagnostics);

    static constexpr const char *kBuiltins[] = {"write", "writeln", "read", "readln"};
    for (const char *builtin : kBuiltins) {
        (void)table.declare(SymbolKind::Builtin, builtin, program.range.begin);
    }

    (void)table.declare(SymbolKind::Program, program.name, program.range.begin);
    walkBlock(table, program.block);
}

} // namespace apollo::pascal
