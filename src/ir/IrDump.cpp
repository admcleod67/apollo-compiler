#include "apollo/ir/IrDump.hpp"

#include <ostream>

namespace apollo::ir {
namespace {

void indent(std::ostream &out, int depth) {
    for (int i = 0; i < depth; ++i) {
        out << "  ";
    }
}

const char *irTypeName(IrType type) {
    switch (type) {
    case IrType::I32:
        return "i32";
    case IrType::F64:
        return "f64";
    case IrType::Bool:
        return "bool";
    case IrType::Char:
        return "char";
    case IrType::StringRef:
        return "string";
    case IrType::ArrayRef:
        return "array";
    case IrType::Void:
        return "void";
    case IrType::Error:
        return "error";
    }
    return "?";
}

const char *opName(Op op) {
    switch (op) {
    case Op::ConstI32:
        return "const.i32";
    case Op::ConstF64:
        return "const.f64";
    case Op::ConstBool:
        return "const.bool";
    case Op::ConstChar:
        return "const.char";
    case Op::ConstString:
        return "const.string";
    case Op::Copy:
        return "copy";
    case Op::LoadLocal:
        return "load.local";
    case Op::StoreLocal:
        return "store.local";
    case Op::Add:
        return "add";
    case Op::Sub:
        return "sub";
    case Op::Mul:
        return "mul";
    case Op::Div:
        return "div";
    case Op::Mod:
        return "mod";
    case Op::Neg:
        return "neg";
    case Op::Not:
        return "not";
    case Op::CmpEq:
        return "cmp.eq";
    case Op::CmpNe:
        return "cmp.ne";
    case Op::CmpLt:
        return "cmp.lt";
    case Op::CmpLe:
        return "cmp.le";
    case Op::CmpGt:
        return "cmp.gt";
    case Op::CmpGe:
        return "cmp.ge";
    case Op::Call:
        return "call";
    case Op::CallRuntime:
        return "call.runtime";
    }
    return "?";
}

void dumpValue(std::ostream &out, ValueId value) {
    out << '%' << value.id;
}

void dumpOperand(std::ostream &out, const Operand &operand) {
    switch (operand.kind) {
    case OperandKind::None:
        break;
    case OperandKind::Value:
        dumpValue(out, operand.value);
        break;
    case OperandKind::Local:
        out << "local[" << operand.slot << ']';
        break;
    case OperandKind::Param:
        out << "param[" << operand.slot << ']';
        break;
    }
}

void dumpInstr(std::ostream &out, const Instr &instr, int depth) {
    indent(out, depth);
    dumpValue(out, instr.result);
    out << " = " << opName(instr.op);

    switch (instr.op) {
    case Op::ConstI32:
        out << ' ' << instr.i64;
        break;
    case Op::ConstF64:
        out << ' ' << instr.f64;
        break;
    case Op::ConstBool:
        out << ' ' << (instr.boolean ? "true" : "false");
        break;
    case Op::ConstChar:
        out << " '" << instr.character << '\'';
        break;
    case Op::ConstString:
        out << " '" << instr.text << '\'';
        break;
    case Op::Copy:
    case Op::Neg:
    case Op::Not:
        out << ' ';
        dumpOperand(out, instr.a);
        break;
    case Op::LoadLocal:
    case Op::StoreLocal:
        out << ' ';
        dumpOperand(out, instr.a);
        if (instr.op == Op::StoreLocal) {
            out << ", ";
            dumpOperand(out, instr.b);
        }
        break;
    case Op::Add:
    case Op::Sub:
    case Op::Mul:
    case Op::Div:
    case Op::Mod:
    case Op::CmpEq:
    case Op::CmpNe:
    case Op::CmpLt:
    case Op::CmpLe:
    case Op::CmpGt:
    case Op::CmpGe:
        out << ' ';
        dumpOperand(out, instr.a);
        out << ", ";
        dumpOperand(out, instr.b);
        break;
    case Op::Call:
        out << ' ' << instr.text;
        for (const ValueId arg : instr.args) {
            out << ", ";
            dumpValue(out, arg);
        }
        break;
    case Op::CallRuntime:
        out << " @" << instr.text;
        for (const ValueId arg : instr.args) {
            out << ", ";
            dumpValue(out, arg);
        }
        break;
    }

    out << " : " << irTypeName(instr.type) << '\n';
}

void dumpTerminator(std::ostream &out, const Terminator &term, int depth) {
    indent(out, depth);
    switch (term.kind) {
    case TerminatorKind::Return:
        out << "return";
        if (term.value) {
            out << ' ';
            dumpValue(out, *term.value);
        }
        out << '\n';
        break;
    case TerminatorKind::Branch:
        out << "branch " << term.target << '\n';
        break;
    case TerminatorKind::BranchIf:
        out << "branch.if ";
        if (term.value) {
            dumpValue(out, *term.value);
        }
        out << ", " << term.target << ", " << term.falseTarget << '\n';
        break;
    }
}

void dumpBlock(std::ostream &out, const BasicBlock &block, int depth) {
    indent(out, depth);
    out << "Block " << block.label << '\n';
    for (const Instr &instr : block.body) {
        dumpInstr(out, instr, depth + 1);
    }
    dumpTerminator(out, block.term, depth + 1);
}

void dumpFunction(std::ostream &out, const Function &func, int depth) {
    indent(out, depth);
    out << "Function " << func.name << " -> " << irTypeName(func.returnType) << '\n';
    for (const BasicBlock &block : func.blocks) {
        dumpBlock(out, block, depth + 1);
    }
}

} // namespace

void writeIrDump(std::ostream &out, const Module &module) {
    out << "Module " << module.name << '\n';
    for (const Function &func : module.functions) {
        dumpFunction(out, func, 1);
    }
}

} // namespace apollo::ir
