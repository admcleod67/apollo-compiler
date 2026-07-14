#include "apollo/pascal/Analyse.hpp"

#include "apollo/common/Diagnostic.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace apollo::pascal {
namespace {

TypePtr typeExpr(SymbolTable &table, ast::Expr &expr,
                 apollo::common::DiagnosticEngine &diagnostics);

TypePtr resolveDenoter(SymbolTable &table, const ast::TypeDenoter &denoter,
                       apollo::common::DiagnosticEngine &diagnostics);

bool isNumeric(TypeTag tag) {
    return tag == TypeTag::Integer || tag == TypeTag::Real;
}

bool isError(const TypePtr &type) {
    return !type || canonicalTag(type) == TypeTag::Error;
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

TypePtr typeExpr(SymbolTable &table, ast::Expr &expr,
                 apollo::common::DiagnosticEngine &diagnostics) {
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
        const Symbol *found = table.lookup(expr.text);
        if (!found) {
            diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
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
            // Bare function name in expression position: treat as result type (call-less).
            result = found->type ? found->type : makeError();
            break;
        default:
            diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "'" + expr.text + "' is not a value");
            result = makeError();
            break;
        }
        break;
    }

    case ast::ExprKind::Unary: {
        TypePtr operand = makeError();
        if (expr.left) {
            operand = typeExpr(table, *expr.left, diagnostics);
        }
        result = typeUnary(expr.unaryOp, operand, expr.range.begin, diagnostics);
        break;
    }

    case ast::ExprKind::Binary: {
        TypePtr left = makeError();
        TypePtr right = makeError();
        if (expr.left) {
            left = typeExpr(table, *expr.left, diagnostics);
        }
        if (expr.right) {
            right = typeExpr(table, *expr.right, diagnostics);
        }
        result = typeBinary(expr.binaryOp, left, right, expr.range.begin, diagnostics);
        break;
    }

    case ast::ExprKind::Call: {
        for (auto &arg : expr.args) {
            if (arg) {
                (void)typeExpr(table, *arg, diagnostics);
            }
        }
        const Symbol *found = table.lookup(expr.text);
        if (!found) {
            diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "undeclared identifier '" + expr.text + "'");
            result = makeError();
            break;
        }
        if (found->kind == SymbolKind::Function) {
            result = found->type ? found->type : makeError();
        } else if (found->kind == SymbolKind::Builtin || found->kind == SymbolKind::Procedure) {
            // Stage 3 will check builtins/procedures; Stage 2 marks non-value call result.
            result = makeError();
        } else {
            diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "'" + expr.text + "' is not a function");
            result = makeError();
        }
        break;
    }

    case ast::ExprKind::Group:
        if (expr.left) {
            result = typeExpr(table, *expr.left, diagnostics);
        } else {
            result = makeError();
        }
        break;
    }

    expr.type = result;
    return result;
}

void typeStmt(SymbolTable &table, ast::Stmt &stmt,
              apollo::common::DiagnosticEngine &diagnostics);

void typeCompound(SymbolTable &table, ast::CompoundStmt &compound,
                  apollo::common::DiagnosticEngine &diagnostics) {
    for (auto &stmt : compound.statements) {
        typeStmt(table, stmt, diagnostics);
    }
}

void typeStmt(SymbolTable &table, ast::Stmt &stmt,
              apollo::common::DiagnosticEngine &diagnostics) {
    switch (stmt.kind) {
    case ast::StmtKind::Compound:
        for (auto &inner : stmt.statements) {
            typeStmt(table, inner, diagnostics);
        }
        break;
    case ast::StmtKind::Assign:
        if (stmt.value) {
            (void)typeExpr(table, *stmt.value, diagnostics);
        }
        break;
    case ast::StmtKind::Call:
        for (auto &arg : stmt.args) {
            if (arg) {
                (void)typeExpr(table, *arg, diagnostics);
            }
        }
        break;
    case ast::StmtKind::If:
        if (stmt.condition) {
            (void)typeExpr(table, *stmt.condition, diagnostics);
        }
        if (stmt.thenBranch) {
            typeStmt(table, *stmt.thenBranch, diagnostics);
        }
        if (stmt.elseBranch) {
            typeStmt(table, *stmt.elseBranch, diagnostics);
        }
        break;
    case ast::StmtKind::While:
        if (stmt.condition) {
            (void)typeExpr(table, *stmt.condition, diagnostics);
        }
        if (stmt.thenBranch) {
            typeStmt(table, *stmt.thenBranch, diagnostics);
        }
        break;
    case ast::StmtKind::Repeat:
        for (auto &inner : stmt.statements) {
            typeStmt(table, inner, diagnostics);
        }
        if (stmt.condition) {
            (void)typeExpr(table, *stmt.condition, diagnostics);
        }
        break;
    case ast::StmtKind::For:
        if (stmt.value) {
            (void)typeExpr(table, *stmt.value, diagnostics);
        }
        if (stmt.forLimit) {
            (void)typeExpr(table, *stmt.forLimit, diagnostics);
        }
        if (stmt.thenBranch) {
            typeStmt(table, *stmt.thenBranch, diagnostics);
        }
        break;
    }
}

void walkBlock(SymbolTable &table, ast::Block &block,
               apollo::common::DiagnosticEngine &diagnostics);

void walkSubprogram(SymbolTable &table, ast::Subprogram &sub,
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

void walkBlock(SymbolTable &table, ast::Block &block,
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
    for (auto &sub : block.subprograms) {
        walkSubprogram(table, sub, diagnostics);
    }

    // Type expressions while this block's scope is still active.
    typeCompound(table, block.body, diagnostics);
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
    walkBlock(table, program.block, diagnostics);
    return table;
}

} // namespace apollo::pascal
