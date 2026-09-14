//
// Pascal AST for Milestone 2 (program / statements / expressions / decls).
//

#ifndef APOLLO_PASCAL_AST_AST_HPP
#define APOLLO_PASCAL_AST_AST_HPP

#pragma once

#include "apollo/common/SourceLocation.hpp"
#include "apollo/pascal/Type.hpp"

#include <memory>
#include <optional>
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
    Index,
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
    /// Filled by semantic analyse; null until then.
    apollo::pascal::TypePtr type;
};

enum class TypeKind {
    Named,
    Array,
};

struct TypeDenoter {
    TypeKind kind{};
    apollo::common::SourceRange range{};
    std::string name;
    std::unique_ptr<Expr> indexLow;
    std::unique_ptr<Expr> indexHigh;
    std::unique_ptr<TypeDenoter> element;
};

struct ConstDecl {
    apollo::common::SourceRange range{};
    std::string name;
    std::unique_ptr<Expr> value;
};

struct TypeDecl {
    apollo::common::SourceRange range{};
    std::string name;
    TypeDenoter type;
};

struct VarDecl {
    apollo::common::SourceRange range{};
    std::vector<std::string> names;
    TypeDenoter type;
};

struct ParamDecl {
    apollo::common::SourceRange range{};
    bool isVar{false};
    std::vector<std::string> names;
    TypeDenoter type;
};

struct Block;

struct Subprogram {
    apollo::common::SourceRange range{};
    bool isFunction{false};
    std::string name;
    std::vector<ParamDecl> params;
    std::optional<TypeDenoter> returnType;
    /// Nested block (owned; breaks Block ↔ Subprogram value cycle).
    std::unique_ptr<Block> block;
};

enum class StmtKind {
    Compound,
    Assign,
    Call,
    If,
    While,
    Repeat,
    For,
};

struct Stmt {
    StmtKind kind{};
    apollo::common::SourceRange range{};
    /// Assign LHS, call callee, or for-loop control variable.
    std::string name;
    /// When set on Assign, LHS is `name[index]` rather than a bare variable.
    std::unique_ptr<Expr> index;
    std::unique_ptr<Expr> value;
    std::vector<std::unique_ptr<Expr>> args;
    std::vector<Stmt> statements;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
    std::unique_ptr<Expr> forLimit;
    bool forDownto{false};
};

struct CompoundStmt {
    apollo::common::SourceRange range{};
    std::vector<Stmt> statements;
};

struct Block {
    apollo::common::SourceRange range{};
    std::vector<ConstDecl> consts;
    std::vector<TypeDecl> types;
    std::vector<VarDecl> vars;
    std::vector<Subprogram> subprograms;
    CompoundStmt body;
};

struct Program {
    apollo::common::SourceRange range{};
    std::string name;
    Block block;
};

} // namespace apollo::pascal::ast

#endif // APOLLO_PASCAL_AST_AST_HPP
