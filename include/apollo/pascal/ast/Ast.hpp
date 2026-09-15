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
    Select,
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
    Record,
};

struct TypeDenoter;

struct RecordFieldDecl {
    std::vector<std::string> names;
    std::unique_ptr<TypeDenoter> type;
};

struct TypeDenoter {
    TypeKind kind{};
    apollo::common::SourceRange range{};
    std::string name;
    std::unique_ptr<Expr> indexLow;
    std::unique_ptr<Expr> indexHigh;
    std::unique_ptr<TypeDenoter> element;
    std::vector<RecordFieldDecl> fields;
    /// Filled by semantic analyse; null until then.
    apollo::pascal::TypePtr resolved;
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
    Case,
};

struct Stmt;

struct CaseLabel {
    std::unique_ptr<Expr> lo;
    /// When set, this label is the closed range `lo .. hi`.
    std::unique_ptr<Expr> hi;
};

struct CaseArm {
    std::vector<CaseLabel> labels;
    std::unique_ptr<Stmt> body;

    CaseArm();
    CaseArm(CaseArm &&) noexcept;
    CaseArm &operator=(CaseArm &&) noexcept;
    ~CaseArm();
};

struct Stmt {
    StmtKind kind{};
    apollo::common::SourceRange range{};
    /// Assign LHS, call callee, or for-loop control variable.
    std::string name;
    /// When set on Assign, LHS is `name[index]` rather than a bare variable.
    std::unique_ptr<Expr> index;
    /// When non-empty on Assign, LHS is `name.field` (optional `index` on the field).
    std::string fieldName;
    std::unique_ptr<Expr> value;
    std::vector<std::unique_ptr<Expr>> args;
    std::vector<Stmt> statements;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
    std::unique_ptr<Expr> forLimit;
    bool forDownto{false};
    /// Case arms when `kind == Case`; selector is `condition`.
    std::vector<CaseArm> caseArms;
};

inline CaseArm::CaseArm() = default;
inline CaseArm::CaseArm(CaseArm &&) noexcept = default;
inline CaseArm &CaseArm::operator=(CaseArm &&) noexcept = default;
inline CaseArm::~CaseArm() = default;

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
