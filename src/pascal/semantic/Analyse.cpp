#include "apollo/pascal/Analyse.hpp"

#include "apollo/common/Diagnostic.hpp"

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

TypePtr resolveDenoter(SymbolTable &table, const ast::TypeDenoter &denoter,
                       apollo::common::DiagnosticEngine &diagnostics);

bool isNumeric(TypeTag tag) {
    return tag == TypeTag::Integer || tag == TypeTag::Real;
}

bool isError(const TypePtr &type) {
    return !type || canonicalTag(type) == TypeTag::Error;
}

bool isAssignable(const TypePtr &dest, const TypePtr &src) {
    if (isError(dest) || isError(src)) {
        return true;
    }
    const TypeTag d = canonicalTag(dest);
    const TypeTag s = canonicalTag(src);
    if (d == s) {
        return true;
    }
    return d == TypeTag::Real && s == TypeTag::Integer;
}

bool isPrintable(TypeTag tag) {
    return tag == TypeTag::Integer || tag == TypeTag::Real || tag == TypeTag::Char ||
           tag == TypeTag::String;
}

bool isReadable(TypeTag tag) {
    return tag == TypeTag::Integer || tag == TypeTag::Real || tag == TypeTag::Char;
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
        case SymbolKind::Function:
            result = found->type ? found->type : makeError();
            break;
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
    }

    const SymbolKind kind = sub.isFunction ? SymbolKind::Function : SymbolKind::Procedure;
    (void)ctx.table.declare(kind, sub.name, sub.range.begin, std::move(returnType));
    Symbol *subSym = ctx.table.lookupMutable(sub.name);

    ctx.table.pushScope();
    for (const auto &param : sub.params) {
        TypePtr paramType = resolveDenoter(ctx.table, param.type, ctx.diagnostics);
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
        (void)ctx.table.declare(SymbolKind::Const, decl.name, decl.range.begin, std::move(type));
    }
    for (const auto &decl : block.types) {
        TypePtr underlying = resolveDenoter(ctx.table, decl.type, ctx.diagnostics);
        TypePtr alias = makeAlias(decl.name, std::move(underlying));
        (void)ctx.table.declare(SymbolKind::Type, decl.name, decl.range.begin, std::move(alias));
    }
    for (const auto &decl : block.vars) {
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
