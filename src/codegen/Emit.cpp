#include "apollo/codegen/Emit.hpp"

#include "apollo/codegen/TbcWriter.hpp"
#include "apollo/common/Diagnostic.hpp"
#include "apollo/common/SourceLocation.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace apollo::codegen {
namespace {

using apollo::common::DiagnosticSeverity;
using apollo::common::SourceLocation;
namespace irs = apollo::ir;

void reportError(apollo::common::DiagnosticEngine &diagnostics, std::string message) {
    diagnostics.report(DiagnosticSeverity::Error, SourceLocation{}, std::move(message));
}

std::string mangleSlot(const std::string &functionName, const std::string &declName) {
    return functionName + "$" + declName;
}

std::string mangleTemp(const std::string &functionName, irs::ValueId value) {
    return functionName + "$t" + std::to_string(value.id);
}

std::string slotName(const irs::Function &function, const irs::Operand &operand) {
    if (operand.kind == irs::OperandKind::Local) {
        if (operand.slot >= function.locals.size()) {
            return function.name + "$local" + std::to_string(operand.slot);
        }
        return mangleSlot(function.name, function.locals[operand.slot].name);
    }
    if (operand.kind == irs::OperandKind::Param) {
        if (operand.slot >= function.params.size()) {
            return function.name + "$param" + std::to_string(operand.slot);
        }
        return mangleSlot(function.name, function.params[operand.slot].name);
    }
    return function.name + "$?";
}

struct EmitCtx {
    const irs::Function &function;
    apollo::common::DiagnosticEngine &diagnostics;
    TbcWriter writer;
    /// Value currently on top of the Gemini operand stack, if any.
    std::optional<irs::ValueId> stackTop;
    /// Temps that have been spilled to `STORE_VAR` mangled temp slots.
    std::unordered_set<std::uint32_t> spilled;
    bool failed{false};

    void fail(std::string message) {
        if (!failed) {
            reportError(diagnostics, std::move(message));
            failed = true;
        }
    }
};

void spillStackTop(EmitCtx &ctx) {
    if (!ctx.stackTop) {
        return;
    }
    const irs::ValueId value = *ctx.stackTop;
    ctx.writer.op("STORE_VAR", mangleTemp(ctx.function.name, value));
    ctx.spilled.insert(value.id);
    ctx.stackTop.reset();
}

void loadSpilled(EmitCtx &ctx, irs::ValueId value) {
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, value));
    ctx.stackTop = value;
}

/// Ensure `value` is on top of the stack (spill whatever else is there first).
void ensureOnTop(EmitCtx &ctx, irs::ValueId value) {
    if (ctx.stackTop && ctx.stackTop->id == value.id) {
        return;
    }
    spillStackTop(ctx);
    if (ctx.spilled.count(value.id) == 0) {
        ctx.fail("codegen: value %" + std::to_string(value.id) +
                 " is not on the stack and was never spilled");
        return;
    }
    loadSpilled(ctx, value);
}

void emitPushInt(EmitCtx &ctx, std::int64_t value) {
    ctx.writer.pushInt(value);
}

/// Spill any stack top, then LOAD left, LOAD right, op → result on stack.
void emitBinary(EmitCtx &ctx, const char *opcode, irs::ValueId left, irs::ValueId right,
                irs::ValueId result) {
    spillStackTop(ctx);
    if (ctx.spilled.count(left.id) == 0) {
        ctx.fail("codegen: left operand %" + std::to_string(left.id) + " unavailable");
        return;
    }
    if (ctx.spilled.count(right.id) == 0) {
        ctx.fail("codegen: right operand %" + std::to_string(right.id) + " unavailable");
        return;
    }
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, left));
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, right));
    ctx.writer.op(opcode);
    ctx.stackTop = result;
}

void emitUnaryNeg(EmitCtx &ctx, irs::ValueId operand, irs::ValueId result) {
    spillStackTop(ctx);
    if (ctx.spilled.count(operand.id) == 0) {
        ctx.fail("codegen: neg operand unavailable");
        return;
    }
    // 0 - x
    emitPushInt(ctx, 0);
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, operand));
    ctx.writer.op("SUB");
    ctx.stackTop = result;
}

void emitUnaryNot(EmitCtx &ctx, irs::ValueId operand, irs::ValueId result) {
    spillStackTop(ctx);
    if (ctx.spilled.count(operand.id) == 0) {
        ctx.fail("codegen: not operand unavailable");
        return;
    }
    // x == 0 → 1 else 0
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, operand));
    emitPushInt(ctx, 0);
    ctx.writer.op("EQ");
    ctx.stackTop = result;
}

void emitAnd(EmitCtx &ctx, irs::ValueId left, irs::ValueId right, irs::ValueId result) {
    // 0/1 MUL
    emitBinary(ctx, "MUL", left, right, result);
}

void emitOr(EmitCtx &ctx, irs::ValueId left, irs::ValueId right, irs::ValueId result) {
    // (left + right) != 0
    emitBinary(ctx, "ADD", left, right, result);
    // result currently on stack; compare to 0
    spillStackTop(ctx);
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, result));
    emitPushInt(ctx, 0);
    ctx.writer.op("NE");
    ctx.stackTop = result;
}

irs::ValueId operandValue(const irs::Operand &operand) {
    return operand.value;
}

void emitConst(EmitCtx &ctx, const irs::Instr &instr) {
    spillStackTop(ctx);
    switch (instr.op) {
    case irs::Op::ConstI32:
        emitPushInt(ctx, instr.i64);
        break;
    case irs::Op::ConstBool:
        emitPushInt(ctx, instr.boolean ? 1 : 0);
        break;
    case irs::Op::ConstChar:
        emitPushInt(ctx, static_cast<unsigned char>(instr.character));
        break;
    case irs::Op::ConstString:
        ctx.writer.pushStr(instr.text);
        break;
    default:
        ctx.fail("codegen: internal const emit error");
        return;
    }
    ctx.stackTop = instr.result;
}

void emitLoadLocal(EmitCtx &ctx, const irs::Instr &instr) {
    spillStackTop(ctx);
    ctx.writer.op("LOAD_VAR", slotName(ctx.function, instr.a));
    ctx.stackTop = instr.result;
}

void emitStoreLocal(EmitCtx &ctx, const irs::Instr &instr) {
    const irs::ValueId value = operandValue(instr.b);
    ensureOnTop(ctx, value);
    ctx.writer.op("STORE_VAR", slotName(ctx.function, instr.a));
    ctx.stackTop.reset(); // STORE_VAR pops
    // Void result is unused; do not leave it on stack.
}

void emitCallRuntime(EmitCtx &ctx, const irs::Instr &instr) {
    const std::string &name = instr.text;
    if (name == "write" || name == "writeln") {
        for (const irs::ValueId arg : instr.args) {
            ensureOnTop(ctx, arg);
            // Prefer PRINT_VAL for mixed types (Gemini BASIC style).
            ctx.writer.op("PRINT_VAL");
            ctx.stackTop.reset();
        }
        if (name == "writeln") {
            ctx.writer.op("PRINT_EOL");
        }
        return;
    }
    if (name == "read" || name == "readln") {
        // M4 emits one CallRuntime per variable (no args); result type selects input op.
        spillStackTop(ctx);
        if (instr.type == irs::IrType::I32 || instr.type == irs::IrType::Bool) {
            ctx.writer.op("INPUT_INT");
        } else if (instr.type == irs::IrType::StringRef || instr.type == irs::IrType::Char) {
            ctx.writer.op("INPUT_STR");
        } else {
            ctx.fail("codegen: unsupported read type for '" + name + "'");
            return;
        }
        ctx.stackTop = instr.result;
        return;
    }
    ctx.fail("codegen: unknown runtime call '" + name + "'");
}

const char *cmpOpcode(irs::Op op) {
    switch (op) {
    case irs::Op::CmpEq:
        return "EQ";
    case irs::Op::CmpNe:
        return "NE";
    case irs::Op::CmpLt:
        return "LT";
    case irs::Op::CmpLe:
        return "LE";
    case irs::Op::CmpGt:
        return "GT";
    case irs::Op::CmpGe:
        return "GE";
    default:
        return nullptr;
    }
}

void emitInstr(EmitCtx &ctx, const irs::Instr &instr) {
    if (ctx.failed) {
        return;
    }

    switch (instr.op) {
    case irs::Op::ConstI32:
    case irs::Op::ConstBool:
    case irs::Op::ConstChar:
    case irs::Op::ConstString:
        emitConst(ctx, instr);
        // Spilling is deferred until the value must leave the stack; but if the next
        // instruction does not consume this value immediately as stack top, setResult
        // path handles it. For consts we leave on stack. When a *new* value is produced
        // later, spillStackTop saves this const.
        // Problem: binary emit requires operands in `spilled`. So before any binary/unary
        // that isn't "stackTop is the only operand", we need spills.
        // Fix: after every value-producing instr, eagerly spill so operands are always
        // reloadable. That matches "spill incumbent before producing new value" and
        // also makes emitBinary simple.
        spillStackTop(ctx);
        break;
    case irs::Op::LoadLocal:
        emitLoadLocal(ctx, instr);
        spillStackTop(ctx);
        break;
    case irs::Op::StoreLocal:
        emitStoreLocal(ctx, instr);
        break;
    case irs::Op::Add:
        emitBinary(ctx, "ADD", operandValue(instr.a), operandValue(instr.b), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::Sub:
        emitBinary(ctx, "SUB", operandValue(instr.a), operandValue(instr.b), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::Mul:
        emitBinary(ctx, "MUL", operandValue(instr.a), operandValue(instr.b), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::Div:
        emitBinary(ctx, "DIV", operandValue(instr.a), operandValue(instr.b), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::Neg:
        emitUnaryNeg(ctx, operandValue(instr.a), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::Not:
        emitUnaryNot(ctx, operandValue(instr.a), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::And:
        emitAnd(ctx, operandValue(instr.a), operandValue(instr.b), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::Or:
        emitOr(ctx, operandValue(instr.a), operandValue(instr.b), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::CmpEq:
    case irs::Op::CmpNe:
    case irs::Op::CmpLt:
    case irs::Op::CmpLe:
    case irs::Op::CmpGt:
    case irs::Op::CmpGe:
        emitBinary(ctx, cmpOpcode(instr.op), operandValue(instr.a), operandValue(instr.b),
                   instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::CallRuntime:
        emitCallRuntime(ctx, instr);
        if (instr.type != irs::IrType::Void && ctx.stackTop) {
            spillStackTop(ctx);
        }
        break;
    case irs::Op::ConstF64:
        ctx.fail("codegen: ConstF64 not supported in Milestone 5 Stage 2");
        break;
    case irs::Op::Mod:
        ctx.fail("codegen: Mod not supported in Milestone 5 Stage 2");
        break;
    case irs::Op::Copy:
        ctx.fail("codegen: Copy not supported in Milestone 5 Stage 2");
        break;
    case irs::Op::Call:
        ctx.fail("codegen: user Call not supported until Milestone 5 Stage 3");
        break;
    }
}

bool validateStraightLineMain(const irs::Module &module,
                              apollo::common::DiagnosticEngine &diagnostics) {
    if (module.functions.empty()) {
        reportError(diagnostics, "codegen: module has no functions");
        return false;
    }
    if (module.functions.size() != 1) {
        reportError(diagnostics,
                    "codegen: only a single main function is supported in Stage 2 "
                    "(found " +
                        std::to_string(module.functions.size()) + ")");
        return false;
    }
    const irs::Function &fn = module.functions[0];
    if (fn.name != "main") {
        reportError(diagnostics, "codegen: expected function 'main', found '" + fn.name + "'");
        return false;
    }
    if (fn.blocks.size() != 1) {
        reportError(diagnostics,
                    "codegen: multi-block CFGs not supported until Milestone 5 Stage 3");
        return false;
    }
    const irs::BasicBlock &block = fn.blocks[0];
    if (block.term.kind != irs::TerminatorKind::Return) {
        reportError(diagnostics,
                    "codegen: branches not supported until Milestone 5 Stage 3");
        return false;
    }
    for (const irs::Instr &instr : block.body) {
        if (instr.op == irs::Op::Call) {
            reportError(diagnostics,
                        "codegen: user Call not supported until Milestone 5 Stage 3");
            return false;
        }
    }
    return true;
}

} // namespace

std::string emitTbc(const irs::Module &module, apollo::common::DiagnosticEngine &diagnostics) {
    if (!validateStraightLineMain(module, diagnostics)) {
        return {};
    }

    const irs::Function &fn = module.functions[0];
    EmitCtx ctx{fn, diagnostics, {}, std::nullopt, {}, false};

    ctx.writer.label("main");
    for (const irs::Instr &instr : fn.blocks[0].body) {
        emitInstr(ctx, instr);
        if (ctx.failed) {
            return {};
        }
    }
    // main Return → HALT (discard optional return value).
    spillStackTop(ctx);
    ctx.writer.op("HALT");

    if (ctx.failed || diagnostics.errorCount() != 0) {
        return {};
    }
    return ctx.writer.str();
}

void writeTbc(std::ostream &out, const irs::Module &module,
              apollo::common::DiagnosticEngine &diagnostics) {
    out << emitTbc(module, diagnostics);
}

} // namespace apollo::codegen
