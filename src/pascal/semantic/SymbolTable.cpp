#include "apollo/pascal/SymbolTable.hpp"

#include "apollo/common/Diagnostic.hpp"

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

TypePtr resolveDenoter(SymbolTable &table, const ast::TypeDenoter &denoter,
                       apollo::common::DiagnosticEngine &diagnostics);

TypePtr typeOfLiteralExpr(const ast::Expr *expr,
                          apollo::common::DiagnosticEngine &diagnostics) {
    if (!expr) {
        return makeError();
    }

    switch (expr->kind) {
    case ast::ExprKind::IntegerLiteral:
        return makePredefined(TypeTag::Integer);
    case ast::ExprKind::RealLiteral:
        return makePredefined(TypeTag::Real);
    case ast::ExprKind::CharLiteral:
        return makePredefined(TypeTag::Char);
    case ast::ExprKind::StringLiteral:
        return makePredefined(TypeTag::String);
    case ast::ExprKind::Group:
        return typeOfLiteralExpr(expr->left.get(), diagnostics);
    default:
        diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr->range.begin,
                           "const initializer must be a literal");
        return makeError();
    }
}

TypePtr resolveDenoter(SymbolTable &table, const ast::TypeDenoter &denoter,
                       apollo::common::DiagnosticEngine &diagnostics) {
    if (denoter.kind == ast::TypeKind::Array) {
        if (!denoter.element) {
            return makeError();
        }
        return makeArray(resolveDenoter(table, *denoter.element, diagnostics));
    }

    // Named type
    if (denoter.name.empty()) {
        return makeError();
    }

    const Symbol *found = table.lookup(denoter.name);
    if (!found || found->kind != SymbolKind::Type || !found->type) {
        diagnostics.report(apollo::common::DiagnosticSeverity::Error, denoter.range.begin,
                           "unknown type '" + denoter.name + "'");
        return makeError();
    }
    return found->type;
}

void walkBlock(SymbolTable &table, const ast::Block &block,
               apollo::common::DiagnosticEngine &diagnostics);

void walkSubprogram(SymbolTable &table, const ast::Subprogram &sub,
                    apollo::common::DiagnosticEngine &diagnostics) {
    TypePtr returnType;
    if (sub.isFunction) {
        if (sub.returnType) {
            returnType = resolveDenoter(table, *sub.returnType, diagnostics);
        } else {
            returnType = makeError();
        }
    }

    const SymbolKind kind = sub.isFunction ? SymbolKind::Function : SymbolKind::Procedure;
    (void)table.declare(kind, sub.name, sub.range.begin, std::move(returnType));

    table.pushScope();
    for (const auto &param : sub.params) {
        TypePtr paramType = resolveDenoter(table, param.type, diagnostics);
        for (const auto &name : param.names) {
            (void)table.declare(SymbolKind::Param, name, param.range.begin, paramType,
                                param.isVar);
        }
    }
    if (sub.block) {
        walkBlock(table, *sub.block, diagnostics);
    }
    table.popScope();
}

void walkBlock(SymbolTable &table, const ast::Block &block,
               apollo::common::DiagnosticEngine &diagnostics) {
    for (const auto &decl : block.consts) {
        TypePtr type = typeOfLiteralExpr(decl.value.get(), diagnostics);
        (void)table.declare(SymbolKind::Const, decl.name, decl.range.begin, std::move(type));
    }
    for (const auto &decl : block.types) {
        TypePtr underlying = resolveDenoter(table, decl.type, diagnostics);
        TypePtr alias = makeAlias(decl.name, std::move(underlying));
        (void)table.declare(SymbolKind::Type, decl.name, decl.range.begin, std::move(alias));
    }
    for (const auto &decl : block.vars) {
        TypePtr type = resolveDenoter(table, decl.type, diagnostics);
        for (const auto &name : decl.names) {
            (void)table.declare(SymbolKind::Var, name, decl.range.begin, type);
        }
    }
    for (const auto &sub : block.subprograms) {
        walkSubprogram(table, sub, diagnostics);
    }
}

void seedPredefinedTypes(SymbolTable &table, apollo::common::SourceLocation location) {
    struct Seed {
        const char *name;
        TypeTag tag;
    };
    static constexpr Seed kTypes[] = {
        {"integer", TypeTag::Integer}, {"real", TypeTag::Real},
        {"boolean", TypeTag::Boolean}, {"char", TypeTag::Char},
        {"string", TypeTag::String},
    };
    for (const Seed &seed : kTypes) {
        (void)table.declare(SymbolKind::Type, seed.name, location, makePredefined(seed.tag));
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

SymbolTable buildSymbolTable(const ast::Program &program,
                             apollo::common::DiagnosticEngine &diagnostics) {
    SymbolTable table(diagnostics);

    seedPredefinedTypes(table, program.range.begin);

    static constexpr const char *kBuiltins[] = {"write", "writeln", "read", "readln"};
    for (const char *builtin : kBuiltins) {
        (void)table.declare(SymbolKind::Builtin, builtin, program.range.begin, makeError());
    }

    (void)table.declare(SymbolKind::Program, program.name, program.range.begin);
    walkBlock(table, program.block, diagnostics);
    return table;
}

} // namespace apollo::pascal
