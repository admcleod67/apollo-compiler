#include "apollo/pascal/AstDump.hpp"

#include <ostream>

namespace apollo::pascal {
namespace {

void indent(std::ostream &out, int depth) {
    for (int i = 0; i < depth; ++i) {
        out << "  ";
    }
}

const char *binaryOpName(ast::BinaryOp op) {
    switch (op) {
    case ast::BinaryOp::Plus:
        return "+";
    case ast::BinaryOp::Minus:
        return "-";
    case ast::BinaryOp::Star:
        return "*";
    case ast::BinaryOp::Slash:
        return "/";
    case ast::BinaryOp::Div:
        return "div";
    case ast::BinaryOp::Mod:
        return "mod";
    case ast::BinaryOp::And:
        return "and";
    case ast::BinaryOp::Or:
        return "or";
    case ast::BinaryOp::Equal:
        return "=";
    case ast::BinaryOp::NotEqual:
        return "<>";
    case ast::BinaryOp::Less:
        return "<";
    case ast::BinaryOp::LessEqual:
        return "<=";
    case ast::BinaryOp::Greater:
        return ">";
    case ast::BinaryOp::GreaterEqual:
        return ">=";
    }
    return "?";
}

const char *unaryOpName(ast::UnaryOp op) {
    switch (op) {
    case ast::UnaryOp::Plus:
        return "+";
    case ast::UnaryOp::Minus:
        return "-";
    case ast::UnaryOp::Not:
        return "not";
    }
    return "?";
}

void dumpExpr(std::ostream &out, const ast::Expr &expr, int depth);
void dumpStmt(std::ostream &out, const ast::Stmt &stmt, int depth);
void dumpType(std::ostream &out, const ast::TypeDenoter &type, int depth);
void dumpBlock(std::ostream &out, const ast::Block &block, int depth);

void dumpType(std::ostream &out, const ast::TypeDenoter &type, int depth) {
    indent(out, depth);
    if (type.kind == ast::TypeKind::Named) {
        out << "NamedType " << type.name << '\n';
        return;
    }
    out << "ArrayType\n";
    if (type.indexLow) {
        indent(out, depth + 1);
        out << "Low\n";
        dumpExpr(out, *type.indexLow, depth + 2);
    }
    if (type.indexHigh) {
        indent(out, depth + 1);
        out << "High\n";
        dumpExpr(out, *type.indexHigh, depth + 2);
    }
    if (type.element) {
        indent(out, depth + 1);
        out << "Element\n";
        dumpType(out, *type.element, depth + 2);
    }
}

void dumpExpr(std::ostream &out, const ast::Expr &expr, int depth) {
    indent(out, depth);
    switch (expr.kind) {
    case ast::ExprKind::Identifier:
        out << "Identifier " << expr.text << '\n';
        break;
    case ast::ExprKind::IntegerLiteral:
        out << "IntegerLiteral " << expr.text << '\n';
        break;
    case ast::ExprKind::RealLiteral:
        out << "RealLiteral " << expr.text << '\n';
        break;
    case ast::ExprKind::StringLiteral:
        out << "StringLiteral " << expr.text << '\n';
        break;
    case ast::ExprKind::CharLiteral:
        out << "CharLiteral " << expr.text << '\n';
        break;
    case ast::ExprKind::Unary:
        out << "UnaryExpr " << unaryOpName(expr.unaryOp) << '\n';
        if (expr.left) {
            dumpExpr(out, *expr.left, depth + 1);
        }
        break;
    case ast::ExprKind::Binary:
        out << "BinaryExpr " << binaryOpName(expr.binaryOp) << '\n';
        if (expr.left) {
            dumpExpr(out, *expr.left, depth + 1);
        }
        if (expr.right) {
            dumpExpr(out, *expr.right, depth + 1);
        }
        break;
    case ast::ExprKind::Call:
        out << "CallExpr " << expr.text << '\n';
        for (const auto &arg : expr.args) {
            if (arg) {
                indent(out, depth + 1);
                out << "Arg\n";
                dumpExpr(out, *arg, depth + 2);
            }
        }
        break;
    case ast::ExprKind::Group:
        out << "GroupExpr\n";
        if (expr.left) {
            dumpExpr(out, *expr.left, depth + 1);
        }
        break;
    }
}

void dumpCallArg(std::ostream &out, const ast::Expr &arg, int depth) {
    indent(out, depth);
    out << "Arg ";
    switch (arg.kind) {
    case ast::ExprKind::StringLiteral:
        out << "StringLiteral " << arg.text << '\n';
        break;
    case ast::ExprKind::CharLiteral:
        out << "CharLiteral " << arg.text << '\n';
        break;
    case ast::ExprKind::IntegerLiteral:
        out << "IntegerLiteral " << arg.text << '\n';
        break;
    case ast::ExprKind::RealLiteral:
        out << "RealLiteral " << arg.text << '\n';
        break;
    case ast::ExprKind::Identifier:
        out << "Identifier " << arg.text << '\n';
        break;
    default:
        out << '\n';
        dumpExpr(out, arg, depth + 1);
        break;
    }
}

void dumpStmt(std::ostream &out, const ast::Stmt &stmt, int depth) {
    indent(out, depth);
    switch (stmt.kind) {
    case ast::StmtKind::Compound:
        out << "CompoundStmt\n";
        for (const auto &child : stmt.statements) {
            dumpStmt(out, child, depth + 1);
        }
        break;
    case ast::StmtKind::Assign:
        out << "AssignStmt " << stmt.name << '\n';
        if (stmt.value) {
            dumpExpr(out, *stmt.value, depth + 1);
        }
        break;
    case ast::StmtKind::Call:
        out << "CallStmt " << stmt.name << '\n';
        for (const auto &arg : stmt.args) {
            if (arg) {
                dumpCallArg(out, *arg, depth + 1);
            }
        }
        break;
    case ast::StmtKind::If:
        out << "IfStmt\n";
        if (stmt.condition) {
            indent(out, depth + 1);
            out << "Cond\n";
            dumpExpr(out, *stmt.condition, depth + 2);
        }
        if (stmt.thenBranch) {
            indent(out, depth + 1);
            out << "Then\n";
            dumpStmt(out, *stmt.thenBranch, depth + 2);
        }
        if (stmt.elseBranch) {
            indent(out, depth + 1);
            out << "Else\n";
            dumpStmt(out, *stmt.elseBranch, depth + 2);
        }
        break;
    case ast::StmtKind::While:
        out << "WhileStmt\n";
        if (stmt.condition) {
            indent(out, depth + 1);
            out << "Cond\n";
            dumpExpr(out, *stmt.condition, depth + 2);
        }
        if (stmt.thenBranch) {
            indent(out, depth + 1);
            out << "Body\n";
            dumpStmt(out, *stmt.thenBranch, depth + 2);
        }
        break;
    case ast::StmtKind::Repeat:
        out << "RepeatStmt\n";
        for (const auto &child : stmt.statements) {
            dumpStmt(out, child, depth + 1);
        }
        if (stmt.condition) {
            indent(out, depth + 1);
            out << "Until\n";
            dumpExpr(out, *stmt.condition, depth + 2);
        }
        break;
    case ast::StmtKind::For:
        out << "ForStmt " << stmt.name << (stmt.forDownto ? " downto" : " to") << '\n';
        if (stmt.value) {
            indent(out, depth + 1);
            out << "From\n";
            dumpExpr(out, *stmt.value, depth + 2);
        }
        if (stmt.forLimit) {
            indent(out, depth + 1);
            out << "To\n";
            dumpExpr(out, *stmt.forLimit, depth + 2);
        }
        if (stmt.thenBranch) {
            indent(out, depth + 1);
            out << "Body\n";
            dumpStmt(out, *stmt.thenBranch, depth + 2);
        }
        break;
    }
}

void dumpBlock(std::ostream &out, const ast::Block &block, int depth) {
    indent(out, depth);
    out << "Block\n";
    for (const auto &decl : block.consts) {
        indent(out, depth + 1);
        out << "ConstDecl " << decl.name << '\n';
        if (decl.value) {
            dumpExpr(out, *decl.value, depth + 2);
        }
    }
    for (const auto &decl : block.types) {
        indent(out, depth + 1);
        out << "TypeDecl " << decl.name << '\n';
        dumpType(out, decl.type, depth + 2);
    }
    for (const auto &decl : block.vars) {
        for (const auto &name : decl.names) {
            indent(out, depth + 1);
            out << "VarDecl " << name << " : ";
            if (decl.type.kind == ast::TypeKind::Named) {
                out << decl.type.name << '\n';
            } else {
                out << "array\n";
                dumpType(out, decl.type, depth + 2);
            }
        }
    }
    for (const auto &sub : block.subprograms) {
        indent(out, depth + 1);
        out << (sub.isFunction ? "FunctionDecl " : "ProcedureDecl ") << sub.name << '\n';
        for (const auto &param : sub.params) {
            for (const auto &name : param.names) {
                indent(out, depth + 2);
                out << "Param " << (param.isVar ? "var " : "") << name << " : ";
                if (param.type.kind == ast::TypeKind::Named) {
                    out << param.type.name << '\n';
                } else {
                    out << "array\n";
                    dumpType(out, param.type, depth + 3);
                }
            }
        }
        if (sub.returnType) {
            indent(out, depth + 2);
            out << "ReturnType\n";
            dumpType(out, *sub.returnType, depth + 3);
        }
        if (sub.block) {
            dumpBlock(out, *sub.block, depth + 2);
        }
    }
    indent(out, depth + 1);
    out << "CompoundStmt\n";
    for (const auto &child : block.body.statements) {
        dumpStmt(out, child, depth + 2);
    }
}

} // namespace

void writeAstDump(std::ostream &out, const ast::Program &program) {
    out << "Program " << program.name << '\n';
    dumpBlock(out, program.block, 1);
}

} // namespace apollo::pascal

