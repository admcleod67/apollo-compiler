#include "apollo/pascal/Analyse.hpp"

#include "apollo/common/Diagnostic.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace apollo::pascal {
namespace {

struct AnalyseCtx {
    SymbolTable &table;
    apollo::common::DiagnosticEngine &diagnostics;
    std::string currentFunction; // folded lower; empty outside a function body
};

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

TypePtr typeExpr(AnalyseCtx &ctx, ast::Expr &expr);
void checkCall(AnalyseCtx &ctx, std::string_view name,
               std::vector<std::unique_ptr<ast::Expr>> &args,
               apollo::common::SourceLocation location, bool asExpression);
void checkStmt(AnalyseCtx &ctx, ast::Stmt &stmt);

TypePtr resolveDenoter(SymbolTable &table, ast::TypeDenoter &denoter,
                       apollo::common::DiagnosticEngine &diagnostics);

bool isNumeric(TypeTag tag) {
    return tag == TypeTag::Integer || tag == TypeTag::Real;
}

bool isError(const TypePtr &type) {
    return !type || canonicalTag(type) == TypeTag::Error;
}

/// Walk Alias → canonical until a non-alias (or null) type is reached.
/// Needed by `isAssignable` so Array element pointers survive peeling (unlike
/// `canonicalTag`, which only returns the tag).
TypePtr peelAliases(TypePtr type) {
    while (type && type->tag == TypeTag::Alias) {
        type = type->canonical;
    }
    return type;
}

bool isAssignable(const TypePtr &dest, const TypePtr &src) {
    if (isError(dest) || isError(src)) {
        return true;
    }
    const TypePtr d = peelAliases(dest);
    const TypePtr s = peelAliases(src);
    if (!d || !s) {
        return true;
    }
    if (d->tag == TypeTag::Array && s->tag == TypeTag::Array) {
        return isAssignable(d->element, s->element);
    }
    if (d->tag == s->tag) {
        return true;
    }
    return d->tag == TypeTag::Real && s->tag == TypeTag::Integer;
}

bool isPrintable(TypeTag tag) {
    return tag == TypeTag::Integer || tag == TypeTag::Real || tag == TypeTag::Char ||
           tag == TypeTag::String;
}

bool isReadable(TypeTag tag) {
    return tag == TypeTag::Integer || tag == TypeTag::Real || tag == TypeTag::Char;
}

/// `real` literals outside `double` range would otherwise be silently emitted as 0.
void checkRealLiteralRange(const ast::Expr &expr,
                           apollo::common::DiagnosticEngine &diagnostics) {
    try {
        (void)std::stod(expr.text);
    } catch (...) {
        diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                           "real literal out of range");
    }
}

TypePtr typeOfLiteralExpr(const ast::Expr *expr,
                          apollo::common::DiagnosticEngine &diagnostics) {
    if (!expr) {
        return makeError();
    }

    switch (expr->kind) {
    case ast::ExprKind::IntegerLiteral:
        return makePredefined(TypeTag::Integer);
    case ast::ExprKind::RealLiteral:
        checkRealLiteralRange(*expr, diagnostics);
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

std::optional<std::int64_t> evalConstInt(SymbolTable &table, const ast::Expr *expr,
                                         apollo::common::DiagnosticEngine &diagnostics) {
    if (!expr) {
        return std::nullopt;
    }
    if (expr->kind == ast::ExprKind::Group) {
        return evalConstInt(table, expr->left.get(), diagnostics);
    }
    if (expr->kind == ast::ExprKind::IntegerLiteral) {
        try {
            return std::stoll(expr->text);
        } catch (...) {
            diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr->range.begin,
                               "invalid integer literal in array bound");
            return std::nullopt;
        }
    }
    if (expr->kind == ast::ExprKind::Unary && expr->unaryOp == ast::UnaryOp::Minus &&
        expr->left) {
        const auto inner = evalConstInt(table, expr->left.get(), diagnostics);
        if (!inner) {
            return std::nullopt;
        }
        return -*inner;
    }
    if (expr->kind == ast::ExprKind::Identifier) {
        const Symbol *found = table.lookup(expr->text);
        if (found && found->kind == SymbolKind::Const && found->constExpr) {
            return evalConstInt(table, found->constExpr, diagnostics);
        }
        diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr->range.begin,
                           "array bound must be a constant integer");
        return std::nullopt;
    }
    diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr->range.begin,
                       "array bound must be a constant integer");
    return std::nullopt;
}

TypePtr resolveDenoterImpl(SymbolTable &table, ast::TypeDenoter &denoter,
                           apollo::common::DiagnosticEngine &diagnostics) {
    if (denoter.kind == ast::TypeKind::Array) {
        if (!denoter.element) {
            return makeError();
        }
        const auto low = evalConstInt(table, denoter.indexLow.get(), diagnostics);
        const auto high = evalConstInt(table, denoter.indexHigh.get(), diagnostics);
        if (!low || !high) {
            return makeError();
        }
        if (*low > *high) {
            diagnostics.report(apollo::common::DiagnosticSeverity::Error, denoter.range.begin,
                               "array lower bound exceeds upper bound");
            return makeError();
        }
        // Gemini stores indices and array sizes in a 32-bit int.
        constexpr auto kVmIntMin = static_cast<std::int64_t>(INT32_MIN);
        constexpr auto kVmIntMax = static_cast<std::int64_t>(INT32_MAX);
        if (*low < kVmIntMin || *high > kVmIntMax || (*high - *low + 1) > kVmIntMax) {
            diagnostics.report(apollo::common::DiagnosticSeverity::Error, denoter.range.begin,
                               "array bounds exceed the target VM's 32-bit integer range");
            return makeError();
        }
        return makeArray(resolveDenoter(table, *denoter.element, diagnostics), *low, *high);
    }

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

/// Resolve `denoter` and record the result on the node.
///
/// IR lowering reads `TypeDenoter::resolved` instead of re-resolving: each subprogram's
/// scope is popped once `analyse()` returns, so its params, locals and local `type`
/// declarations are no longer reachable through `SymbolTable`.
TypePtr resolveDenoter(SymbolTable &table, ast::TypeDenoter &denoter,
                       apollo::common::DiagnosticEngine &diagnostics) {
    denoter.resolved = resolveDenoterImpl(table, denoter, diagnostics);
    return denoter.resolved;
}

TypePtr typeBinary(ast::BinaryOp op, const TypePtr &left, const TypePtr &right,
                   apollo::common::SourceLocation location,
                   apollo::common::DiagnosticEngine &diagnostics) {
    if (isError(left) || isError(right)) {
        return makeError();
    }

    const TypeTag lt = canonicalTag(left);
    const TypeTag rt = canonicalTag(right);

    auto mismatch = [&]() {
        diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                           "incompatible operand types for operator");
        return makeError();
    };

    switch (op) {
    case ast::BinaryOp::Plus:
    case ast::BinaryOp::Minus:
    case ast::BinaryOp::Star:
        if (!isNumeric(lt) || !isNumeric(rt)) {
            return mismatch();
        }
        if (lt == TypeTag::Integer && rt == TypeTag::Integer) {
            return makePredefined(TypeTag::Integer);
        }
        return makePredefined(TypeTag::Real);

    case ast::BinaryOp::Slash:
        if (!isNumeric(lt) || !isNumeric(rt)) {
            return mismatch();
        }
        return makePredefined(TypeTag::Real);

    case ast::BinaryOp::Div:
    case ast::BinaryOp::Mod:
        if (lt != TypeTag::Integer || rt != TypeTag::Integer) {
            return mismatch();
        }
        return makePredefined(TypeTag::Integer);

    case ast::BinaryOp::And:
    case ast::BinaryOp::Or:
        if (lt != TypeTag::Boolean || rt != TypeTag::Boolean) {
            return mismatch();
        }
        return makePredefined(TypeTag::Boolean);

    case ast::BinaryOp::Equal:
    case ast::BinaryOp::NotEqual:
    case ast::BinaryOp::Less:
    case ast::BinaryOp::LessEqual:
    case ast::BinaryOp::Greater:
    case ast::BinaryOp::GreaterEqual: {
        const bool numeric = isNumeric(lt) && isNumeric(rt);
        const bool sameOrdinal = (lt == rt) && (lt == TypeTag::Char || lt == TypeTag::Boolean ||
                                                lt == TypeTag::Integer || lt == TypeTag::Real);
        if (!numeric && !sameOrdinal) {
            return mismatch();
        }
        return makePredefined(TypeTag::Boolean);
    }
    }

    return makeError();
}

TypePtr typeUnary(ast::UnaryOp op, const TypePtr &operand,
                  apollo::common::SourceLocation location,
                  apollo::common::DiagnosticEngine &diagnostics) {
    if (isError(operand)) {
        return makeError();
    }

    const TypeTag tag = canonicalTag(operand);
    switch (op) {
    case ast::UnaryOp::Plus:
    case ast::UnaryOp::Minus:
        if (tag == TypeTag::Integer || tag == TypeTag::Real) {
            return makePredefined(tag);
        }
        diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                           "unary +/- requires a numeric operand");
        return makeError();
    case ast::UnaryOp::Not:
        if (tag == TypeTag::Boolean) {
            return makePredefined(TypeTag::Boolean);
        }
        diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                           "not requires a boolean operand");
        return makeError();
    }
    return makeError();
}

void checkUserCallArgs(AnalyseCtx &ctx, const Symbol &callee,
                       std::vector<std::unique_ptr<ast::Expr>> &args,
                       apollo::common::SourceLocation location) {
    if (args.size() != callee.paramTypes.size()) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                               "wrong number of arguments for '" + callee.name + "'");
        return;
    }

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (!args[i]) {
            continue;
        }
        const TypePtr &paramType = callee.paramTypes[i];
        const bool isVar = i < callee.paramIsVar.size() && callee.paramIsVar[i];

        if (isVar) {
            if (args[i]->kind != ast::ExprKind::Identifier) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                       args[i]->range.begin,
                                       "var parameter requires a variable");
                continue;
            }
            const Symbol *argSym = ctx.table.lookup(args[i]->text);
            if (!argSym ||
                (argSym->kind != SymbolKind::Var && argSym->kind != SymbolKind::Param)) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                       args[i]->range.begin,
                                       "var parameter requires a variable");
                continue;
            }
            if (!isAssignable(paramType, args[i]->type)) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                       args[i]->range.begin,
                                       "argument type incompatible with parameter");
            }
        } else if (!isAssignable(paramType, args[i]->type)) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                   args[i]->range.begin,
                                   "argument type incompatible with parameter");
        }
    }
}

void checkBuiltinCall(AnalyseCtx &ctx, const Symbol &callee,
                      std::vector<std::unique_ptr<ast::Expr>> &args,
                      apollo::common::SourceLocation location) {
    const std::string name = foldAsciiLower(callee.name);
    const bool isWrite = name == "write" || name == "writeln";
    const bool isRead = name == "read" || name == "readln";

    if (isWrite) {
        for (auto &arg : args) {
            if (!arg) {
                continue;
            }
            if (isError(arg->type)) {
                continue;
            }
            if (!isPrintable(canonicalTag(arg->type))) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                       arg->range.begin,
                                       "argument type not printable for '" + callee.name + "'");
            }
        }
        return;
    }

    if (isRead) {
        if (args.empty()) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                                   "'" + callee.name + "' requires at least one argument");
            return;
        }
        for (auto &arg : args) {
            if (!arg) {
                continue;
            }
            if (arg->kind != ast::ExprKind::Identifier) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                       arg->range.begin,
                                       "'" + callee.name + "' argument must be a variable");
                continue;
            }
            const Symbol *argSym = ctx.table.lookup(arg->text);
            if (!argSym ||
                (argSym->kind != SymbolKind::Var && argSym->kind != SymbolKind::Param)) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                       arg->range.begin,
                                       "'" + callee.name + "' argument must be a variable");
                continue;
            }
            if (isError(arg->type)) {
                continue;
            }
            if (!isReadable(canonicalTag(arg->type))) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                       arg->range.begin,
                                       "argument type not readable for '" + callee.name + "'");
            }
        }
        return;
    }

    ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                           "unsupported builtin '" + callee.name + "'");
}

void checkCall(AnalyseCtx &ctx, std::string_view name,
               std::vector<std::unique_ptr<ast::Expr>> &args,
               apollo::common::SourceLocation location, bool asExpression) {
    for (auto &arg : args) {
        if (arg) {
            (void)typeExpr(ctx, *arg);
        }
    }

    const Symbol *found = ctx.table.lookup(name);
    if (!found) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                               "undeclared identifier '" + std::string(name) + "'");
        return;
    }

    switch (found->kind) {
    case SymbolKind::Builtin:
        if (asExpression) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                                   "'" + std::string(name) + "' cannot be used as a value");
        }
        checkBuiltinCall(ctx, *found, args, location);
        break;
    case SymbolKind::Procedure:
        if (asExpression) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                                   "procedure '" + std::string(name) +
                                       "' cannot be used as a value");
        }
        checkUserCallArgs(ctx, *found, args, location);
        break;
    case SymbolKind::Function:
        checkUserCallArgs(ctx, *found, args, location);
        break;
    default:
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, location,
                               "'" + std::string(name) + "' is not callable");
        break;
    }
}

void requireBooleanCondition(AnalyseCtx &ctx, ast::Expr *condition) {
    if (!condition || isError(condition->type)) {
        return;
    }
    if (canonicalTag(condition->type) != TypeTag::Boolean) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                               condition->range.begin, "condition must be boolean");
    }
}

TypePtr typeExpr(AnalyseCtx &ctx, ast::Expr &expr) {
    TypePtr result = makeError();

    switch (expr.kind) {
    case ast::ExprKind::IntegerLiteral:
        result = makePredefined(TypeTag::Integer);
        break;
    case ast::ExprKind::RealLiteral:
        checkRealLiteralRange(expr, ctx.diagnostics);
        result = makePredefined(TypeTag::Real);
        break;
    case ast::ExprKind::CharLiteral:
        result = makePredefined(TypeTag::Char);
        break;
    case ast::ExprKind::StringLiteral:
        result = makePredefined(TypeTag::String);
        break;

    case ast::ExprKind::Identifier: {
        const Symbol *found = ctx.table.lookup(expr.text);
        if (!found) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                                   "undeclared identifier '" + expr.text + "'");
            result = makeError();
            break;
        }
        switch (found->kind) {
        case SymbolKind::Const:
        case SymbolKind::Var:
        case SymbolKind::Param:
            result = found->type ? found->type : makeError();
            break;
        case SymbolKind::Function: {
            // Bare function name is a zero-argument call (arity checked).
            std::vector<std::unique_ptr<ast::Expr>> noArgs;
            checkUserCallArgs(ctx, *found, noArgs, expr.range.begin);
            result = found->type ? found->type : makeError();
            break;
        }
        default:
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                                   "'" + expr.text + "' is not a value");
            result = makeError();
            break;
        }
        break;
    }

    case ast::ExprKind::Unary: {
        TypePtr operand = makeError();
        if (expr.left) {
            operand = typeExpr(ctx, *expr.left);
        }
        result = typeUnary(expr.unaryOp, operand, expr.range.begin, ctx.diagnostics);
        break;
    }

    case ast::ExprKind::Binary: {
        TypePtr left = makeError();
        TypePtr right = makeError();
        if (expr.left) {
            left = typeExpr(ctx, *expr.left);
        }
        if (expr.right) {
            right = typeExpr(ctx, *expr.right);
        }
        result = typeBinary(expr.binaryOp, left, right, expr.range.begin, ctx.diagnostics);
        break;
    }

    case ast::ExprKind::Call: {
        checkCall(ctx, expr.text, expr.args, expr.range.begin, /*asExpression=*/true);
        const Symbol *found = ctx.table.lookup(expr.text);
        if (found && found->kind == SymbolKind::Function) {
            result = found->type ? found->type : makeError();
        } else {
            result = makeError();
        }
        break;
    }

    case ast::ExprKind::Group:
        if (expr.left) {
            result = typeExpr(ctx, *expr.left);
        } else {
            result = makeError();
        }
        break;

    case ast::ExprKind::Index: {
        TypePtr baseType = makeError();
        TypePtr indexType = makeError();
        if (expr.left) {
            baseType = typeExpr(ctx, *expr.left);
        }
        if (expr.right) {
            indexType = typeExpr(ctx, *expr.right);
        }
        const TypePtr peeled = peelAliases(baseType);
        if (!isError(baseType) && (!peeled || peeled->tag != TypeTag::Array)) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                                   "indexed expression requires an array");
            result = makeError();
            break;
        }
        if (!isError(indexType) && canonicalTag(indexType) != TypeTag::Integer) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                   expr.right ? expr.right->range.begin : expr.range.begin,
                                   "array index must be integer");
        }
        if (peeled && peeled->element) {
            result = peeled->element;
        } else {
            result = makeError();
        }
        break;
    }
    }

    expr.type = result;
    return result;
}

void checkAssign(AnalyseCtx &ctx, ast::Stmt &stmt) {
    TypePtr rhsType = makeError();
    if (stmt.value) {
        rhsType = typeExpr(ctx, *stmt.value);
    }

    const Symbol *lhs = ctx.table.lookup(stmt.name);
    if (!lhs) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "undeclared identifier '" + stmt.name + "'");
        return;
    }

    TypePtr destType;
    if (lhs->kind == SymbolKind::Var || lhs->kind == SymbolKind::Param) {
        destType = lhs->type;
    } else if (lhs->kind == SymbolKind::Function &&
               foldAsciiLower(lhs->name) == ctx.currentFunction) {
        destType = lhs->type;
    } else {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "'" + stmt.name + "' is not assignable");
        return;
    }

    if (stmt.index) {
        TypePtr indexType = typeExpr(ctx, *stmt.index);
        if (!isError(indexType) && canonicalTag(indexType) != TypeTag::Integer) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                   stmt.index->range.begin, "array index must be integer");
        }
        const TypePtr peeled = peelAliases(destType);
        if (!isError(destType) && (!peeled || peeled->tag != TypeTag::Array)) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                                   "indexed assignment requires an array");
            return;
        }
        destType = peeled && peeled->element ? peeled->element : makeError();
    } else {
        const TypePtr peeled = peelAliases(destType);
        if (peeled && peeled->tag == TypeTag::Array) {
            if (!isAssignable(destType, rhsType)) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                                       "incompatible types in assignment");
                return;
            }
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                                   "whole-array assignment not supported yet");
            return;
        }
    }

    if (!isAssignable(destType, rhsType)) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "incompatible types in assignment");
    }
}

void checkFor(AnalyseCtx &ctx, ast::Stmt &stmt) {
    TypePtr startType = makeError();
    TypePtr limitType = makeError();
    if (stmt.value) {
        startType = typeExpr(ctx, *stmt.value);
    }
    if (stmt.forLimit) {
        limitType = typeExpr(ctx, *stmt.forLimit);
    }

    const Symbol *control = ctx.table.lookup(stmt.name);
    if (!control) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "undeclared identifier '" + stmt.name + "'");
    } else if (control->kind != SymbolKind::Var && control->kind != SymbolKind::Param) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "for control variable must be a variable");
    } else if (!isError(control->type) &&
               canonicalTag(control->type) != TypeTag::Integer) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "for control variable must be integer");
    }

    if (!isError(startType) && canonicalTag(startType) != TypeTag::Integer) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                               stmt.value ? stmt.value->range.begin : stmt.range.begin,
                               "for bound must be integer");
    }
    if (!isError(limitType) && canonicalTag(limitType) != TypeTag::Integer) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                               stmt.forLimit ? stmt.forLimit->range.begin : stmt.range.begin,
                               "for bound must be integer");
    }

    if (stmt.thenBranch) {
        checkStmt(ctx, *stmt.thenBranch);
    }
}

void checkStmt(AnalyseCtx &ctx, ast::Stmt &stmt) {
    switch (stmt.kind) {
    case ast::StmtKind::Compound:
        for (auto &inner : stmt.statements) {
            checkStmt(ctx, inner);
        }
        break;
    case ast::StmtKind::Assign:
        checkAssign(ctx, stmt);
        break;
    case ast::StmtKind::Call:
        checkCall(ctx, stmt.name, stmt.args, stmt.range.begin, /*asExpression=*/false);
        break;
    case ast::StmtKind::If:
        if (stmt.condition) {
            (void)typeExpr(ctx, *stmt.condition);
            requireBooleanCondition(ctx, stmt.condition.get());
        }
        if (stmt.thenBranch) {
            checkStmt(ctx, *stmt.thenBranch);
        }
        if (stmt.elseBranch) {
            checkStmt(ctx, *stmt.elseBranch);
        }
        break;
    case ast::StmtKind::While:
        if (stmt.condition) {
            (void)typeExpr(ctx, *stmt.condition);
            requireBooleanCondition(ctx, stmt.condition.get());
        }
        if (stmt.thenBranch) {
            checkStmt(ctx, *stmt.thenBranch);
        }
        break;
    case ast::StmtKind::Repeat:
        for (auto &inner : stmt.statements) {
            checkStmt(ctx, inner);
        }
        if (stmt.condition) {
            (void)typeExpr(ctx, *stmt.condition);
            requireBooleanCondition(ctx, stmt.condition.get());
        }
        break;
    case ast::StmtKind::For:
        checkFor(ctx, stmt);
        break;
    }
}

void walkBlock(AnalyseCtx &ctx, ast::Block &block);

void walkSubprogram(AnalyseCtx &ctx, ast::Subprogram &sub) {
    TypePtr returnType;
    if (sub.isFunction) {
        if (sub.returnType) {
            returnType = resolveDenoter(ctx.table, *sub.returnType, ctx.diagnostics);
        } else {
            returnType = makeError();
        }
        if (canonicalTag(returnType) == TypeTag::Array) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                   sub.returnType->range.begin,
                                   "function result type must be a simple type");
        }
    }

    const SymbolKind kind = sub.isFunction ? SymbolKind::Function : SymbolKind::Procedure;
    const bool declared =
        ctx.table.declare(kind, sub.name, sub.range.begin, std::move(returnType));
    Symbol *subSym = declared ? ctx.table.lookupMutable(sub.name) : nullptr;

    ctx.table.pushScope();
    for (auto &param : sub.params) {
        TypePtr paramType = resolveDenoter(ctx.table, param.type, ctx.diagnostics);
        // Gemini keeps arrays in a separate store from scalars, so passing one through a
        // param slot would emit a LOAD_VAR of a name that holds no value. Stage 2 adds
        // real support via MAT_COPY; until then the param is still declared so the body
        // does not cascade "undeclared identifier".
        if (canonicalTag(paramType) == TypeTag::Array) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, param.range.begin,
                                   "array parameters not supported yet");
        }
        if (subSym) {
            for (std::size_t i = 0; i < param.names.size(); ++i) {
                subSym->paramTypes.push_back(paramType);
                subSym->paramIsVar.push_back(param.isVar);
            }
        }
        for (const auto &name : param.names) {
            (void)ctx.table.declare(SymbolKind::Param, name, param.range.begin, paramType,
                                    param.isVar);
        }
    }

    const std::string previousFunction = ctx.currentFunction;
    if (sub.isFunction) {
        ctx.currentFunction = foldAsciiLower(sub.name);
    }
    if (sub.block) {
        walkBlock(ctx, *sub.block);
    }
    ctx.currentFunction = previousFunction;
    ctx.table.popScope();
}

void walkBlock(AnalyseCtx &ctx, ast::Block &block) {
    for (const auto &decl : block.consts) {
        TypePtr type = typeOfLiteralExpr(decl.value.get(), ctx.diagnostics);
        if (ctx.table.declare(SymbolKind::Const, decl.name, decl.range.begin, std::move(type))) {
            if (Symbol *sym = ctx.table.lookupMutable(decl.name)) {
                sym->constExpr = decl.value.get();
            }
        }
    }
    for (auto &decl : block.types) {
        TypePtr underlying = resolveDenoter(ctx.table, decl.type, ctx.diagnostics);
        TypePtr alias = makeAlias(decl.name, std::move(underlying));
        (void)ctx.table.declare(SymbolKind::Type, decl.name, decl.range.begin, std::move(alias));
    }
    for (auto &decl : block.vars) {
        TypePtr type = resolveDenoter(ctx.table, decl.type, ctx.diagnostics);
        for (const auto &name : decl.names) {
            (void)ctx.table.declare(SymbolKind::Var, name, decl.range.begin, type);
        }
    }
    for (auto &sub : block.subprograms) {
        walkSubprogram(ctx, sub);
    }

    for (auto &stmt : block.body.statements) {
        checkStmt(ctx, stmt);
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

void seedBooleanConsts(SymbolTable &table, apollo::common::SourceLocation location) {
    TypePtr booleanType = makePredefined(TypeTag::Boolean);
    (void)table.declare(SymbolKind::Const, "true", location, booleanType);
    (void)table.declare(SymbolKind::Const, "false", location, booleanType);
}

} // namespace

SymbolTable analyse(ast::Program &program,
                    apollo::common::DiagnosticEngine &diagnostics) {
    SymbolTable table(diagnostics);

    seedPredefinedTypes(table, program.range.begin);
    seedBooleanConsts(table, program.range.begin);

    static constexpr const char *kBuiltins[] = {"write", "writeln", "read", "readln"};
    for (const char *builtin : kBuiltins) {
        (void)table.declare(SymbolKind::Builtin, builtin, program.range.begin, makeError());
    }

    (void)table.declare(SymbolKind::Program, program.name, program.range.begin);

    AnalyseCtx ctx{table, diagnostics, {}};
    walkBlock(ctx, program.block);
    return table;
}

} // namespace apollo::pascal
