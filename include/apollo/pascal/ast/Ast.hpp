//
// Pascal AST for Milestone 2 (program / statements / expressions).
//

#ifndef APOLLO_PASCAL_AST_AST_HPP
#define APOLLO_PASCAL_AST_AST_HPP

#pragma once

#include "apollo/common/SourceLocation.hpp"

#include <memory>
#include <string>
#include <vector>

namespace apollo::pascal::ast {

enum class BinaryOp {
    Plus,
    Minus,
    Star,
    Slash,
    Div,
    Mod,
    And,
    Or,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

enum class UnaryOp {
    Plus,
    Minus,
    Not,
};

enum class ExprKind {
    Identifier,
    IntegerLiteral,
    RealLiteral,
    StringLiteral,
    CharLiteral,
    Unary,
    Binary,
    Call,
    Group,
};

struct Expr {
    ExprKind kind{};
    apollo::common::SourceRange range{};
    /// Identifier spelling or literal lexeme (owned copy of token lexeme).
    std::string text;
    UnaryOp unaryOp{};
    BinaryOp binaryOp{};
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::vector<std::unique_ptr<Expr>> args;
};

enum class StmtKind {
    Compound,
    Assign,
    Call,
};

struct Stmt {
    StmtKind kind{};
    apollo::common::SourceRange range{};
    /// Assign LHS or call callee.
    std::string name;
    std::unique_ptr<Expr> value;
    std::vector<std::unique_ptr<Expr>> args;
    std::vector<Stmt> statements;
};

struct CompoundStmt {
    apollo::common::SourceRange range{};
    std::vector<Stmt> statements;
};

struct Block {
    apollo::common::SourceRange range{};
    CompoundStmt body;
};

struct Program {
    apollo::common::SourceRange range{};
    std::string name;
    Block block;
};

} // namespace apollo::pascal::ast

#endif // APOLLO_PASCAL_AST_AST_HPP
