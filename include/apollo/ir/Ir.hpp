//
// Shared language-neutral IR for Apollo (Milestone 4).
//

#ifndef APOLLO_IR_IR_HPP
#define APOLLO_IR_IR_HPP

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace apollo::ir {

enum class IrType {
    I32,
    F64,
    Bool,
    Char,
    StringRef,
    ArrayRef,
    Void,
    Error,
};

enum class Op {
    ConstI32,
    ConstF64,
    ConstBool,
    ConstChar,
    ConstString,
    Copy,
    LoadLocal,
    StoreLocal,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,
    Not,
    CmpEq,
    CmpNe,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,
    Call,
    CallRuntime,
};

enum class TerminatorKind {
    Return,
    Branch,
    BranchIf,
};

struct ValueId {
    std::uint32_t id{0};

    [[nodiscard]] bool operator==(const ValueId &) const = default;
};

enum class OperandKind {
    None,
    Value,
    Local,
    Param,
};

struct Operand {
    OperandKind kind{OperandKind::None};
    ValueId value{};
    std::uint32_t slot{0};
};

struct Instr {
    Op op{};
    IrType type{IrType::Error};
    ValueId result{};
    Operand a{};
    Operand b{};
    /// Const payload or callee / runtime name.
    std::string text;
    std::int64_t i64{0};
    double f64{0.0};
    bool boolean{false};
    char character{'\0'};
    std::vector<ValueId> args;
};

struct Terminator {
    TerminatorKind kind{TerminatorKind::Return};
    std::optional<ValueId> value;
    std::string target;
    std::string falseTarget;
};

struct BasicBlock {
    std::string label;
    std::vector<Instr> body;
    Terminator term{};
};

struct Param {
    std::string name;
    IrType type{IrType::Error};
};

struct Local {
    std::string name;
    IrType type{IrType::Error};
};

struct Function {
    std::string name;
    IrType returnType{IrType::Void};
    std::vector<Param> params;
    std::vector<Local> locals;
    std::vector<BasicBlock> blocks;
    std::uint32_t nextTemp{0};

    [[nodiscard]] ValueId newTemp();
};

struct Module {
    std::string name;
    std::vector<Function> functions;
};

} // namespace apollo::ir

#endif // APOLLO_IR_IR_HPP
