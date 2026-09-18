#include "apollo/codegen/Emit.hpp"

#include "apollo/codegen/TbcWriter.hpp"
#include "apollo/common/Diagnostic.hpp"
#include "apollo/common/SourceLocation.hpp"

#include <cstdint>
#include <limits>
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

/// Entry block (`entry`) → `functionName`; others → `functionName$irLabel`.
std::string mangleBlockLabel(const std::string &functionName, const std::string &blockLabel) {
    if (blockLabel == "entry" || blockLabel.empty()) {
        return functionName;
    }
    return functionName + "$" + blockLabel;
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
    TbcWriter &writer;
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

    [[nodiscard]] bool isMain() const { return function.name == "main"; }
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
    emitPushInt(ctx, 0);
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, operand));
    ctx.writer.op("SUB");
    ctx.stackTop = result;
}

/// Gemini has no int → float opcode (only `CoerceInt` the other way), but any `double`
/// operand forces a `double` result (`arithmeticResultValue` in the VM `Runtime`), and the
/// product is exact for every `int32`. A single `COERCE_FLT` would replace this pair if
/// the VM ever grows one.
void emitConvertF64(EmitCtx &ctx, irs::ValueId operand, irs::ValueId result) {
    spillStackTop(ctx);
    if (ctx.spilled.count(operand.id) == 0) {
        ctx.fail("codegen: convert operand unavailable");
        return;
    }
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, operand));
    ctx.writer.pushFlt(1.0);
    ctx.writer.op("MUL");
    ctx.stackTop = result;
}

void emitConvertI32(EmitCtx &ctx, irs::ValueId operand, irs::ValueId result) {
    ensureOnTop(ctx, operand);
    ctx.writer.op("COERCE_INT");
    ctx.stackTop = result;
}

void emitAbsInt(EmitCtx &ctx, irs::ValueId operand, irs::ValueId result) {
    ensureOnTop(ctx, operand);
    ctx.writer.op("ABS_INT");
    ctx.stackTop = result;
}

void emitUnaryNot(EmitCtx &ctx, irs::ValueId operand, irs::ValueId result) {
    spillStackTop(ctx);
    if (ctx.spilled.count(operand.id) == 0) {
        ctx.fail("codegen: not operand unavailable");
        return;
    }
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, operand));
    emitPushInt(ctx, 0);
    ctx.writer.op("EQ");
    ctx.stackTop = result;
}

void emitAnd(EmitCtx &ctx, irs::ValueId left, irs::ValueId right, irs::ValueId result) {
    emitBinary(ctx, "MUL", left, right, result);
}

void emitOr(EmitCtx &ctx, irs::ValueId left, irs::ValueId right, irs::ValueId result) {
    emitBinary(ctx, "ADD", left, right, result);
    spillStackTop(ctx);
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, result));
    emitPushInt(ctx, 0);
    ctx.writer.op("NE");
    ctx.stackTop = result;
}

irs::ValueId operandValue(const irs::Operand &operand) {
    return operand.value;
}


std::optional<std::int64_t> arrayLowBound(const irs::Function &function, const irs::Operand &operand) {
    if (operand.kind == irs::OperandKind::Local && operand.slot < function.locals.size()) {
        return function.locals[operand.slot].arrayLow;
    }
    if (operand.kind == irs::OperandKind::Param && operand.slot < function.params.size()) {
        return function.params[operand.slot].arrayLow;
    }
    return std::nullopt;
}

void emitMod(EmitCtx &ctx, irs::ValueId left, irs::ValueId right, irs::ValueId result) {
    // a - (a div b) * b on the stack, using Gemini DIV (toward zero).
    spillStackTop(ctx);
    if (ctx.spilled.count(left.id) == 0 || ctx.spilled.count(right.id) == 0) {
        ctx.fail("codegen: mod operands unavailable");
        return;
    }
    const std::string a = mangleTemp(ctx.function.name, left);
    const std::string b = mangleTemp(ctx.function.name, right);
    ctx.writer.op("LOAD_VAR", a);
    ctx.writer.op("LOAD_VAR", a);
    ctx.writer.op("LOAD_VAR", b);
    ctx.writer.op("DIV");
    ctx.writer.op("LOAD_VAR", b);
    ctx.writer.op("MUL");
    ctx.writer.op("SUB");
    ctx.stackTop = result;
}

void emitVmIndex(EmitCtx &ctx, irs::ValueId pascalIndex, std::int64_t arrayLow) {
    // vmIndex = pascalIndex - (arrayLow - 1); omit the subtract when lo is 1.
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, pascalIndex));
    const std::int64_t offset = arrayLow - 1;
    if (offset == 0) {
        return;
    }
    // The VM parses PUSH_INT with std::stoi, so lo == INT32_MIN cannot fold to lo - 1.
    if (offset < std::numeric_limits<std::int32_t>::min()) {
        ctx.writer.pushInt(arrayLow);
        ctx.writer.op("SUB");
        ctx.writer.pushInt(1);
        ctx.writer.op("ADD");
        return;
    }
    ctx.writer.pushInt(offset);
    ctx.writer.op("SUB");
}

void emitDimArray(EmitCtx &ctx, const irs::Instr &instr) {
    spillStackTop(ctx);
    const std::string name = slotName(ctx.function, instr.a);
    const std::int64_t size = instr.i64;
    if (size < 1) {
        ctx.fail("codegen: DimArray size must be >= 1");
        return;
    }
    ctx.writer.pushInt(size);
    ctx.writer.op("DIM_ARRAY", name);

    switch (instr.type) {
    case irs::IrType::StringRef:
        // DIM_ARRAY already fills every element with "".
        break;
    case irs::IrType::F64:
        ctx.writer.pushFlt(0.0);
        ctx.writer.op("MAT_INIT", name);
        break;
    default:
        ctx.writer.pushInt(0);
        ctx.writer.op("MAT_INIT", name);
        break;
    }
}

void emitLoadIndex(EmitCtx &ctx, const irs::Instr &instr) {
    spillStackTop(ctx);
    const auto low = arrayLowBound(ctx.function, instr.a);
    if (!low) {
        ctx.fail("codegen: LoadIndex missing array low bound");
        return;
    }
    const irs::ValueId index = operandValue(instr.b);
    if (ctx.spilled.count(index.id) == 0) {
        ctx.fail("codegen: LoadIndex index unavailable");
        return;
    }
    emitVmIndex(ctx, index, *low);
    ctx.writer.op("LOAD_ARR", slotName(ctx.function, instr.a));
    ctx.stackTop = instr.result;
}

void emitStoreIndex(EmitCtx &ctx, const irs::Instr &instr) {
    spillStackTop(ctx);
    const auto low = arrayLowBound(ctx.function, instr.a);
    if (!low) {
        ctx.fail("codegen: StoreIndex missing array low bound");
        return;
    }
    if (instr.args.empty()) {
        ctx.fail("codegen: StoreIndex missing index");
        return;
    }
    const irs::ValueId value = operandValue(instr.b);
    const irs::ValueId index = instr.args.front();
    if (ctx.spilled.count(value.id) == 0 || ctx.spilled.count(index.id) == 0) {
        ctx.fail("codegen: StoreIndex operands unavailable");
        return;
    }
    ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, value));
    emitVmIndex(ctx, index, *low);
    ctx.writer.op("STORE_ARR", slotName(ctx.function, instr.a));
    ctx.stackTop.reset();
}

void emitConst(EmitCtx &ctx, const irs::Instr &instr) {
    spillStackTop(ctx);
    switch (instr.op) {
    case irs::Op::ConstI32:
        emitPushInt(ctx, instr.i64);
        break;
    case irs::Op::ConstF64:
        ctx.writer.pushFlt(instr.f64);
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
    ctx.stackTop.reset();
}

void emitCallRuntime(EmitCtx &ctx, const irs::Instr &instr) {
    const std::string &name = instr.text;
    if (name == "write" || name == "writeln") {
        for (const irs::ValueId arg : instr.args) {
            ensureOnTop(ctx, arg);
            ctx.writer.op("PRINT_VAL");
            ctx.stackTop.reset();
        }
        if (name == "writeln") {
            ctx.writer.op("PRINT_EOL");
        }
        return;
    }
    if (name == "read" || name == "readln") {
        spillStackTop(ctx);
        if (instr.type == irs::IrType::I32 || instr.type == irs::IrType::Bool) {
            ctx.writer.op("INPUT_INT");
        } else if (instr.type == irs::IrType::F64) {
            // Gemini has no float input opcode; read the line and let the multiply below
            // parse it (VM `coerceToDouble` runs `strtod` over string operands).
            ctx.writer.op("INPUT_STR");
            ctx.writer.pushFlt(1.0);
            ctx.writer.op("MUL");
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

void emitArrayCopy(EmitCtx &ctx, const irs::Instr &instr) {
    const std::string dest = slotName(ctx.function, instr.a);
    const std::string src = slotName(ctx.function, instr.b);
    ctx.writer.op("MAT_COPY", dest + "|" + src);
}

/// Push args left-to-right, CALL callee; spill non-void result.
void emitUserCall(EmitCtx &ctx, const irs::Instr &instr) {
    spillStackTop(ctx);
    for (const irs::ArrayCopySetup &copy : instr.matCopies) {
        if (copy.size < 1) {
            ctx.fail("codegen: array argument copy has invalid size");
            return;
        }
        ctx.writer.pushInt(copy.size);
        ctx.writer.op("DIM_ARRAY", copy.dst);
        switch (copy.element) {
        case irs::IrType::StringRef:
            break;
        case irs::IrType::F64:
            ctx.writer.pushFlt(0.0);
            ctx.writer.op("MAT_INIT", copy.dst);
            break;
        default:
            ctx.writer.pushInt(0);
            ctx.writer.op("MAT_INIT", copy.dst);
            break;
        }
        ctx.writer.op("MAT_COPY", copy.dst + "|" + copy.src);
    }
    for (const irs::ValueId arg : instr.args) {
        if (ctx.spilled.count(arg.id) == 0) {
            ctx.fail("codegen: call argument %" + std::to_string(arg.id) + " unavailable");
            return;
        }
        ctx.writer.op("LOAD_VAR", mangleTemp(ctx.function.name, arg));
    }
    ctx.writer.op("CALL", instr.text);
    ctx.stackTop.reset();
    if (instr.type != irs::IrType::Void) {
        ctx.stackTop = instr.result;
        spillStackTop(ctx);
    }
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
    case irs::Op::ConstF64:
    case irs::Op::ConstBool:
    case irs::Op::ConstChar:
    case irs::Op::ConstString:
        emitConst(ctx, instr);
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
    case irs::Op::Mod:
        emitMod(ctx, operandValue(instr.a), operandValue(instr.b), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::DimArray:
        emitDimArray(ctx, instr);
        break;
    case irs::Op::ArrayCopy:
        emitArrayCopy(ctx, instr);
        break;
    case irs::Op::LoadIndex:
        emitLoadIndex(ctx, instr);
        spillStackTop(ctx);
        break;
    case irs::Op::StoreIndex:
        emitStoreIndex(ctx, instr);
        break;
    case irs::Op::ConvertF64:
        emitConvertF64(ctx, operandValue(instr.a), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::ConvertI32:
        emitConvertI32(ctx, operandValue(instr.a), instr.result);
        spillStackTop(ctx);
        break;
    case irs::Op::Abs:
        emitAbsInt(ctx, operandValue(instr.a), instr.result);
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
    case irs::Op::Call:
        emitUserCall(ctx, instr);
        break;
    case irs::Op::Copy:
        ensureOnTop(ctx, operandValue(instr.a));
        ctx.stackTop = instr.result;
        spillStackTop(ctx);
        break;
    }
}

void emitParamPrologue(EmitCtx &ctx) {
    // Caller pushed args left-to-right; top of stack is the last param.
    for (auto it = ctx.function.params.rbegin(); it != ctx.function.params.rend(); ++it) {
        if (it->type == irs::IrType::ArrayRef) {
            continue;
        }
        ctx.writer.op("STORE_VAR", mangleSlot(ctx.function.name, it->name));
    }
}

void emitTerminator(EmitCtx &ctx, const irs::Terminator &term) {
    if (ctx.failed) {
        return;
    }

    switch (term.kind) {
    case irs::TerminatorKind::Branch:
        spillStackTop(ctx);
        ctx.writer.op("JUMP", mangleBlockLabel(ctx.function.name, term.target));
        break;
    case irs::TerminatorKind::BranchIf: {
        if (!term.value) {
            ctx.fail("codegen: BranchIf missing condition value");
            return;
        }
        ensureOnTop(ctx, *term.value);
        // Gemini JZ jumps when top is zero → false target; else fall through then JUMP true.
        ctx.writer.op("JZ", mangleBlockLabel(ctx.function.name, term.falseTarget));
        ctx.stackTop.reset(); // JZ pops
        ctx.writer.op("JUMP", mangleBlockLabel(ctx.function.name, term.target));
        break;
    }
    case irs::TerminatorKind::Return:
        if (ctx.isMain()) {
            spillStackTop(ctx);
            ctx.writer.op("HALT");
        } else {
            if (term.value) {
                ensureOnTop(ctx, *term.value);
                // Leave return value on stack for the caller; do not spill.
                ctx.stackTop.reset();
            } else {
                spillStackTop(ctx);
            }
            ctx.writer.op("RETURN");
        }
        break;
    }
}

void emitBasicBlock(EmitCtx &ctx, const irs::BasicBlock &block, bool isEntry) {
    ctx.stackTop.reset();
    ctx.writer.label(mangleBlockLabel(ctx.function.name, block.label));

    if (isEntry && !ctx.isMain() && !ctx.function.params.empty()) {
        emitParamPrologue(ctx);
    }

    for (const irs::Instr &instr : block.body) {
        emitInstr(ctx, instr);
        if (ctx.failed) {
            return;
        }
    }
    emitTerminator(ctx, block.term);
}

bool emitFunction(TbcWriter &writer, const irs::Function &function,
                  apollo::common::DiagnosticEngine &diagnostics) {
    if (function.blocks.empty()) {
        reportError(diagnostics, "codegen: function '" + function.name + "' has no blocks");
        return false;
    }

    EmitCtx ctx{function, diagnostics, writer, std::nullopt, {}, false};
    for (std::size_t i = 0; i < function.blocks.size(); ++i) {
        emitBasicBlock(ctx, function.blocks[i], /*isEntry=*/i == 0);
        if (ctx.failed) {
            return false;
        }
    }
    return !ctx.failed;
}

bool validateModule(const irs::Module &module, apollo::common::DiagnosticEngine &diagnostics) {
    if (module.functions.empty()) {
        reportError(diagnostics, "codegen: module has no functions");
        return false;
    }
    if (module.functions[0].name != "main") {
        reportError(diagnostics, "codegen: expected functions[0] to be 'main', found '" +
                                     module.functions[0].name + "'");
        return false;
    }
    return true;
}

} // namespace

std::string emitTbc(const irs::Module &module, apollo::common::DiagnosticEngine &diagnostics) {
    if (!validateModule(module, diagnostics)) {
        return {};
    }

    TbcWriter writer;
    for (const irs::Function &fn : module.functions) {
        if (!emitFunction(writer, fn, diagnostics)) {
            return {};
        }
    }

    if (diagnostics.errorCount() != 0) {
        return {};
    }
    return writer.str();
}

void writeTbc(std::ostream &out, const irs::Module &module,
              apollo::common::DiagnosticEngine &diagnostics) {
    out << emitTbc(module, diagnostics);
}

} // namespace apollo::codegen
