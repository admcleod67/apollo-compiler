#include "apollo/pascal/ir/Lower.hpp"

#include "apollo/common/Diagnostic.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace apollo::pascal::ir {
namespace irs = apollo::ir;
namespace {

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

/// Strip the outer quotes from a raw Pascal string/char lexeme and un-double `''` to `'`.
/// The scanner discards its decoded content, so `Expr::text` is still the raw source span.
std::string decodeQuotedLexeme(std::string_view raw) {
    std::string decoded;
    if (raw.size() < 2) {
        return decoded;
    }
    const std::size_t end = raw.size() - 1;
    std::size_t i = 1;
    while (i < end) {
        if (raw[i] == '\'' && i + 1 < end && raw[i + 1] == '\'') {
            decoded.push_back('\'');
            i += 2;
            continue;
        }
        decoded.push_back(raw[i]);
        ++i;
    }
    return decoded;
}

std::int64_t parseIntegerLiteral(const std::string &text) {
    try {
        return std::stoll(text);
    } catch (...) {
        return 0;
    }
}

double parseRealLiteral(const std::string &text) {
    try {
        return std::stod(text);
    } catch (...) {
        return 0.0;
    }
}

irs::IrType toIrType(const TypePtr &type) {
    if (!type) {
        return irs::IrType::Error;
    }
    switch (canonicalTag(type)) {
    case TypeTag::Integer:
        return irs::IrType::I32;
    case TypeTag::Real:
        return irs::IrType::F64;
    case TypeTag::Boolean:
        return irs::IrType::Bool;
    case TypeTag::Char:
        return irs::IrType::Char;
    case TypeTag::String:
        return irs::IrType::StringRef;
    case TypeTag::Array:
        return irs::IrType::ArrayRef;
    case TypeTag::Record:
        return irs::IrType::Error;
    case TypeTag::Alias: // canonicalTag() already peels aliases
    case TypeTag::Error:
        return irs::IrType::Error;
    }
    return irs::IrType::Error;
}

void fillArrayMeta(irs::Local &local, const TypePtr &type) {
    TypePtr peeled = type;
    while (peeled && peeled->tag == TypeTag::Alias) {
        peeled = peeled->canonical;
    }
    if (!peeled || peeled->tag != TypeTag::Array || !peeled->hasBounds) {
        return;
    }
    local.arrayLow = peeled->indexLow;
    local.arrayHigh = peeled->indexHigh;
    local.arrayElement = toIrType(peeled->element);
}

void fillArrayMeta(irs::Param &param, const TypePtr &type) {
    TypePtr peeled = type;
    while (peeled && peeled->tag == TypeTag::Alias) {
        peeled = peeled->canonical;
    }
    if (!peeled || peeled->tag != TypeTag::Array || !peeled->hasBounds) {
        return;
    }
    param.arrayLow = peeled->indexLow;
    param.arrayHigh = peeled->indexHigh;
    param.arrayElement = toIrType(peeled->element);
}

struct LowerCtx {
    const SymbolTable &symbols;
    apollo::common::DiagnosticEngine &diagnostics;
    irs::Function &function;
    /// Currently-open block; always un-terminated on entry/exit of `lowerStmt`.
    irs::BasicBlock block;
    /// Folded local var name -> `Function::locals` index.
    std::unordered_map<std::string, std::uint32_t> localSlots;
    /// Folded param name -> `Function::params` index.
    std::unordered_map<std::string, std::uint32_t> paramSlots;
    /// Folded const name -> its literal declaration expr (for inlining at each use).
    std::unordered_map<std::string, const ast::Expr *> constExprs;
    /// Record field slots: folded `base$field` -> `Function::locals` index.
    std::unordered_map<std::string, std::uint32_t> fieldSlots;
    /// Whole record variables (no aggregate slot): folded name -> record type.
    std::unordered_map<std::string, TypePtr> recordVars;
    /// Monotonic counter for unique block labels within this function.
    std::uint32_t blockCounter{0};
    /// Folded name of the enclosing function, set only while lowering a function body.
    std::optional<std::string> functionResultName;
    /// Value last assigned to `functionResultName`; flows into the final `Return`.
    irs::ValueId resultValue{};
};

/// Type a param/local declaration from the annotation `analyse()` left on the denoter.
///
/// Re-resolving here is not an option: `SymbolTable` pops each subprogram's scope once
/// `analyse()` returns, so its params, locals and local `type` declarations are gone. A
/// missing annotation means lowering ran on an unanalysed AST, which is a bug rather than
/// a source error — `lowerToIr` only runs after analyse reports zero errors.
TypePtr denoterType(LowerCtx &ctx, const ast::TypeDenoter &denoter) {
    if (!denoter.resolved) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, denoter.range.begin,
                               "IR lowering: type denoter was not resolved by analyse");
        return makeError();
    }
    return denoter.resolved;
}

irs::Operand makeValueOperand(irs::ValueId value) {
    irs::Operand operand;
    operand.kind = irs::OperandKind::Value;
    operand.value = value;
    return operand;
}

irs::Operand makeLocalOperand(std::uint32_t slot) {
    irs::Operand operand;
    operand.kind = irs::OperandKind::Local;
    operand.slot = slot;
    return operand;
}

irs::Operand makeParamOperand(std::uint32_t slot) {
    irs::Operand operand;
    operand.kind = irs::OperandKind::Param;
    operand.slot = slot;
    return operand;
}

struct ResolvedSlot {
    irs::Operand operand;
    irs::IrType type;
};

/// Look up a param or local by folded name, scoped to *this* function only (no access to
/// enclosing-scope locals/globals from within a subprogram — see Stage 3 notes).
std::optional<ResolvedSlot> resolveSlot(const LowerCtx &ctx, const std::string &folded) {
    const auto param = ctx.paramSlots.find(folded);
    if (param != ctx.paramSlots.end()) {
        return ResolvedSlot{makeParamOperand(param->second), ctx.function.params[param->second].type};
    }
    const auto local = ctx.localSlots.find(folded);
    if (local != ctx.localSlots.end()) {
        return ResolvedSlot{makeLocalOperand(local->second), ctx.function.locals[local->second].type};
    }
    return std::nullopt;
}

TypePtr peelTypeAliases(TypePtr type) {
    while (type && type->tag == TypeTag::Alias) {
        type = type->canonical;
    }
    return type;
}

std::string fieldSlotKey(std::string_view baseName, std::string_view fieldName) {
    return foldAsciiLower(baseName) + "$" + foldAsciiLower(fieldName);
}

std::string fieldStorageName(std::string_view baseName, std::string_view fieldName) {
    return std::string(baseName) + "$" + std::string(fieldName);
}

void declareRecordFields(LowerCtx &ctx, std::string_view baseName, const TypePtr &recordType) {
    const TypePtr peeled = peelTypeAliases(recordType);
    if (!peeled || peeled->tag != TypeTag::Record) {
        return;
    }
    for (const RecordField &field : peeled->fields) {
        const std::string storage = fieldStorageName(baseName, field.name);
        const irs::IrType irFieldType = toIrType(field.type);
        irs::Local local{storage, irFieldType};
        fillArrayMeta(local, field.type);
        const auto slot = static_cast<std::uint32_t>(ctx.function.locals.size());
        ctx.function.locals.push_back(std::move(local));
        ctx.fieldSlots.emplace(fieldSlotKey(baseName, field.name), slot);
    }
}

void declareRecordParamFields(LowerCtx &ctx, std::string_view paramName, const TypePtr &recordType,
                              std::uint32_t paramSlot) {
    (void)paramSlot;
    declareRecordFields(ctx, paramName, recordType);
}

std::optional<ResolvedSlot> resolveFieldSlot(const LowerCtx &ctx, std::string_view baseName,
                                             std::string_view fieldName) {
    const auto found = ctx.fieldSlots.find(fieldSlotKey(baseName, fieldName));
    if (found == ctx.fieldSlots.end()) {
        return std::nullopt;
    }
    const std::uint32_t slot = found->second;
    if (slot >= ctx.function.locals.size()) {
        return std::nullopt;
    }
    return ResolvedSlot{makeLocalOperand(slot), ctx.function.locals[slot].type};
}

std::optional<ResolvedSlot> resolveArrayBaseSlot(LowerCtx &ctx, const ast::Expr &base) {
    if (base.kind == ast::ExprKind::Identifier) {
        return resolveSlot(ctx, foldAsciiLower(base.text));
    }
    if (base.kind == ast::ExprKind::Select && base.left &&
        base.left->kind == ast::ExprKind::Identifier) {
        return resolveFieldSlot(ctx, base.left->text, base.text);
    }
    return std::nullopt;
}

irs::ValueId lowerExpr(LowerCtx &ctx, const ast::Expr &expr);
void lowerStmt(LowerCtx &ctx, const ast::Stmt &stmt);
irs::ValueId emitLoad(LowerCtx &ctx, irs::Operand source, irs::IrType type);
void emitStore(LowerCtx &ctx, irs::Operand dest, irs::ValueId value);

void emitArrayCopy(LowerCtx &ctx, irs::Operand dest, irs::Operand src) {
    irs::Instr instr;
    instr.op = irs::Op::ArrayCopy;
    instr.type = irs::IrType::Void;
    instr.a = dest;
    instr.b = src;
    ctx.block.body.push_back(std::move(instr));
}

void copyRecordFields(LowerCtx &ctx, std::string_view destBase, std::string_view srcBase,
                      const TypePtr &recordType) {
    const TypePtr peeled = peelTypeAliases(recordType);
    if (!peeled || peeled->tag != TypeTag::Record) {
        return;
    }
    for (const RecordField &field : peeled->fields) {
        const auto srcSlot = resolveFieldSlot(ctx, srcBase, field.name);
        const auto destSlot = resolveFieldSlot(ctx, destBase, field.name);
        if (!srcSlot || !destSlot) {
            continue;
        }
        const TypePtr fieldType = field.type;
        const irs::IrType irField = toIrType(fieldType);
        const TypePtr fieldPeeled = peelTypeAliases(fieldType);
        if (fieldPeeled && fieldPeeled->tag == TypeTag::Array) {
            emitArrayCopy(ctx, destSlot->operand, srcSlot->operand);
            continue;
        }
        const irs::ValueId value = emitLoad(ctx, srcSlot->operand, irField);
        emitStore(ctx, destSlot->operand, value);
    }
}

irs::ValueId emitConstI32(LowerCtx &ctx, std::int64_t value) {
    irs::Instr instr;
    instr.op = irs::Op::ConstI32;
    instr.type = irs::IrType::I32;
    instr.result = ctx.function.newTemp();
    instr.i64 = value;
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

irs::ValueId emitConstF64(LowerCtx &ctx, double value) {
    irs::Instr instr;
    instr.op = irs::Op::ConstF64;
    instr.type = irs::IrType::F64;
    instr.result = ctx.function.newTemp();
    instr.f64 = value;
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

irs::ValueId emitConstBool(LowerCtx &ctx, bool value) {
    irs::Instr instr;
    instr.op = irs::Op::ConstBool;
    instr.type = irs::IrType::Bool;
    instr.result = ctx.function.newTemp();
    instr.boolean = value;
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

irs::ValueId emitConstChar(LowerCtx &ctx, char value) {
    irs::Instr instr;
    instr.op = irs::Op::ConstChar;
    instr.type = irs::IrType::Char;
    instr.result = ctx.function.newTemp();
    instr.character = value;
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

irs::ValueId emitConstString(LowerCtx &ctx, std::string value) {
    irs::Instr instr;
    instr.op = irs::Op::ConstString;
    instr.type = irs::IrType::StringRef;
    instr.result = ctx.function.newTemp();
    instr.text = std::move(value);
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

irs::ValueId emitZeroValue(LowerCtx &ctx, irs::IrType type) {
    switch (type) {
    case irs::IrType::I32:
        return emitConstI32(ctx, 0);
    case irs::IrType::F64:
        return emitConstF64(ctx, 0.0);
    case irs::IrType::Bool:
        return emitConstBool(ctx, false);
    case irs::IrType::Char:
        return emitConstChar(ctx, '\0');
    case irs::IrType::StringRef:
        return emitConstString(ctx, std::string());
    case irs::IrType::ArrayRef:
    case irs::IrType::Void:
    case irs::IrType::Error:
        return emitConstI32(ctx, 0);
    }
    return emitConstI32(ctx, 0);
}

irs::ValueId emitLoad(LowerCtx &ctx, irs::Operand source, irs::IrType type) {
    irs::Instr instr;
    instr.op = irs::Op::LoadLocal;
    instr.type = type;
    instr.result = ctx.function.newTemp();
    instr.a = source;
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

void emitStore(LowerCtx &ctx, irs::Operand dest, irs::ValueId value) {
    irs::Instr instr;
    instr.op = irs::Op::StoreLocal;
    instr.type = irs::IrType::Void;
    instr.a = dest;
    instr.b = makeValueOperand(value);
    ctx.block.body.push_back(std::move(instr));
}

void emitDimArray(LowerCtx &ctx, irs::Operand dest, std::int64_t size, irs::IrType element) {
    irs::Instr instr;
    instr.op = irs::Op::DimArray;
    instr.type = element;
    instr.a = dest;
    instr.i64 = size;
    ctx.block.body.push_back(std::move(instr));
}

irs::ValueId emitLoadIndex(LowerCtx &ctx, irs::Operand arraySlot, irs::ValueId index,
                           irs::IrType elementType) {
    irs::Instr instr;
    instr.op = irs::Op::LoadIndex;
    instr.type = elementType;
    instr.result = ctx.function.newTemp();
    instr.a = arraySlot;
    instr.b = makeValueOperand(index);
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

void emitStoreIndex(LowerCtx &ctx, irs::Operand arraySlot, irs::ValueId index,
                    irs::ValueId value) {
    irs::Instr instr;
    instr.op = irs::Op::StoreIndex;
    instr.type = irs::IrType::Void;
    instr.a = arraySlot;
    instr.b = makeValueOperand(value);
    instr.args.push_back(index);
    ctx.block.body.push_back(std::move(instr));
}

irs::ValueId emitUnaryOp(LowerCtx &ctx, irs::Op op, irs::ValueId operand, irs::IrType type) {
    irs::Instr instr;
    instr.op = op;
    instr.type = type;
    instr.result = ctx.function.newTemp();
    instr.a = makeValueOperand(operand);
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

irs::ValueId emitBinaryOp(LowerCtx &ctx, irs::Op op, irs::ValueId left, irs::ValueId right,
                          irs::IrType type) {
    irs::Instr instr;
    instr.op = op;
    instr.type = type;
    instr.result = ctx.function.newTemp();
    instr.a = makeValueOperand(left);
    instr.b = makeValueOperand(right);
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

/// Strip `Group` wrappers so `(1)` still reads as an integer literal.
const ast::Expr *peelGroups(const ast::Expr *expr) {
    while (expr && expr->kind == ast::ExprKind::Group) {
        expr = expr->left.get();
    }
    return expr;
}

/// Lower `expr` for a Real context.
///
/// Gemini decides between integer and float arithmetic from the runtime operand types, so
/// an Integer value reaching a Real context has to be widened here or `/` truncates.
/// Integer literals become `ConstF64` outright; everything else gets a `ConvertF64`.
irs::ValueId lowerExprAsReal(LowerCtx &ctx, const ast::Expr &expr) {
    if (const ast::Expr *inner = peelGroups(&expr);
        inner && inner->kind == ast::ExprKind::IntegerLiteral) {
        return emitConstF64(ctx, static_cast<double>(parseIntegerLiteral(inner->text)));
    }
    const irs::ValueId value = lowerExpr(ctx, expr);
    if (toIrType(expr.type) != irs::IrType::I32) {
        return value;
    }
    return emitUnaryOp(ctx, irs::Op::ConvertF64, value, irs::IrType::F64);
}

/// Lower `expr` for a destination of `target` type, widening Integer → Real as needed.
irs::ValueId lowerExprFor(LowerCtx &ctx, const ast::Expr &expr, irs::IrType target) {
    if (target == irs::IrType::F64) {
        return lowerExprAsReal(ctx, expr);
    }
    return lowerExpr(ctx, expr);
}

/// Element type recorded on an array param/local slot by `fillArrayMeta`.
irs::IrType arrayElementType(const LowerCtx &ctx, const irs::Operand &operand) {
    if (operand.kind == irs::OperandKind::Local && operand.slot < ctx.function.locals.size()) {
        return ctx.function.locals[operand.slot].arrayElement;
    }
    if (operand.kind == irs::OperandKind::Param && operand.slot < ctx.function.params.size()) {
        return ctx.function.params[operand.slot].arrayElement;
    }
    return irs::IrType::Error;
}

/// Lower call arguments, widening each one the callee declared as `real`.
std::vector<irs::ValueId> lowerCallArgs(LowerCtx &ctx, const std::string &calleeName,
                                        const std::vector<std::unique_ptr<ast::Expr>> &args,
                                        std::vector<std::string> *matCopies) {
    const Symbol *callee = ctx.symbols.lookup(calleeName);
    std::vector<irs::ValueId> values;
    values.reserve(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (!args[i]) {
            continue;
        }
        irs::IrType paramType = irs::IrType::Error;
        bool isVar = false;
        if (callee && i < callee->paramTypes.size()) {
            paramType = toIrType(callee->paramTypes[i]);
            isVar = i < callee->paramIsVar.size() && callee->paramIsVar[i];
        }
        if (paramType == irs::IrType::ArrayRef && !isVar) {
            if (args[i]->kind != ast::ExprKind::Identifier) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                       args[i]->range.begin,
                                       "IR lowering: array argument must be a variable");
                continue;
            }
            if (matCopies && callee && i < callee->paramNames.size()) {
                const std::string dst =
                    fieldStorageName(calleeName, callee->paramNames[i]);
                const std::string src =
                    fieldStorageName(ctx.function.name, args[i]->text);
                matCopies->push_back(dst + "|" + src);
            }
            continue;
        }
        values.push_back(lowerExprFor(ctx, *args[i], paramType));
    }
    return values;
}

irs::ValueId lowerUserCall(LowerCtx &ctx, const std::string &calleeName,
                           std::vector<irs::ValueId> args, irs::IrType resultType,
                           std::vector<std::string> matCopies) {
    irs::Instr call;
    call.op = irs::Op::Call;
    call.type = resultType;
    if (resultType != irs::IrType::Void) {
        call.result = ctx.function.newTemp();
    }
    call.text = calleeName;
    call.args = std::move(args);
    call.matCopies = std::move(matCopies);
    const irs::ValueId result = call.result;
    ctx.block.body.push_back(std::move(call));
    return result;
}

/// Generate a unique block label within the current function.
std::string newLabel(LowerCtx &ctx, const char *prefix) {
    return prefix + std::to_string(ctx.blockCounter++);
}

irs::Terminator branchTo(std::string target) {
    irs::Terminator term;
    term.kind = irs::TerminatorKind::Branch;
    term.target = std::move(target);
    return term;
}

irs::Terminator branchIfTo(irs::ValueId cond, std::string trueTarget, std::string falseTarget) {
    irs::Terminator term;
    term.kind = irs::TerminatorKind::BranchIf;
    term.value = cond;
    term.target = std::move(trueTarget);
    term.falseTarget = std::move(falseTarget);
    return term;
}

/// Attach `term` to the currently-open block, push it into `Function::blocks`, and reset
/// `ctx.block` to a fresh (unlabeled) block ready for `beginBlock`.
void sealBlock(LowerCtx &ctx, irs::Terminator term) {
    ctx.block.term = std::move(term);
    ctx.function.blocks.push_back(std::move(ctx.block));
    ctx.block = irs::BasicBlock{};
}

void beginBlock(LowerCtx &ctx, std::string label) {
    ctx.block.label = std::move(label);
}

irs::Op toBinaryOp(ast::BinaryOp op) {
    switch (op) {
    case ast::BinaryOp::Plus:
        return irs::Op::Add;
    case ast::BinaryOp::Minus:
        return irs::Op::Sub;
    case ast::BinaryOp::Star:
        return irs::Op::Mul;
    case ast::BinaryOp::Slash:
    case ast::BinaryOp::Div:
        // Real (`/`) vs integer (`div`) division share `Op::Div`; the result `IrType`
        // (F64 vs I32) disambiguates, matching M3's typing rules.
        return irs::Op::Div;
    case ast::BinaryOp::Mod:
        return irs::Op::Mod;
    case ast::BinaryOp::And:
        return irs::Op::And;
    case ast::BinaryOp::Or:
        return irs::Op::Or;
    case ast::BinaryOp::Equal:
        return irs::Op::CmpEq;
    case ast::BinaryOp::NotEqual:
        return irs::Op::CmpNe;
    case ast::BinaryOp::Less:
        return irs::Op::CmpLt;
    case ast::BinaryOp::LessEqual:
        return irs::Op::CmpLe;
    case ast::BinaryOp::Greater:
        return irs::Op::CmpGt;
    case ast::BinaryOp::GreaterEqual:
        return irs::Op::CmpGe;
    }
    return irs::Op::Add;
}

irs::ValueId lowerIdentifier(LowerCtx &ctx, const ast::Expr &expr) {
    const std::string folded = foldAsciiLower(expr.text);

    // Check this function's own params/locals first: after analyse() returns, a
    // subprogram's own scope is popped, so `symbols.lookup()` below can no longer resolve
    // it (or, for a name shared with an outer scope, could mis-resolve to that unrelated
    // symbol instead).
    if (const auto slot = resolveSlot(ctx, folded)) {
        return emitLoad(ctx, slot->operand, slot->type);
    }
    if (ctx.recordVars.count(folded) != 0) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "IR lowering: cannot use a record variable as a whole value ('" +
                                   expr.text + "')");
        return emitConstI32(ctx, 0);
    }

    const Symbol *symbol = ctx.symbols.lookup(expr.text);
    if (!symbol || symbol->kind == SymbolKind::Var || symbol->kind == SymbolKind::Param) {
        // Either genuinely undeclared, or resolved (if at all) to a Var/Param outside this
        // function's own scope (e.g. an outer program-level var) — not modeled yet.
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "IR lowering: accessing enclosing scope locals not "
                               "supported until a later stage ('" +
                                   expr.text + "')");
        return emitConstI32(ctx, 0);
    }

    switch (symbol->kind) {
    case SymbolKind::Const: {
        if (folded == "true") {
            return emitConstBool(ctx, true);
        }
        if (folded == "false") {
            return emitConstBool(ctx, false);
        }
        const auto found = ctx.constExprs.find(folded);
        if (found == ctx.constExprs.end() || !found->second) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                                   "IR lowering: missing constant value for '" + expr.text +
                                       "'");
            return emitConstI32(ctx, 0);
        }
        return lowerExpr(ctx, *found->second);
    }
    case SymbolKind::Function:
        // Bare function identifier is a zero-argument call; M3 already checked arity.
        return lowerUserCall(ctx, expr.text, {}, toIrType(symbol->type), {});
    default:
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "IR lowering: '" + expr.text + "' is not a value");
        return emitConstI32(ctx, 0);
    }
}

irs::ValueId lowerUnary(LowerCtx &ctx, const ast::Expr &expr) {
    const irs::ValueId operand = expr.left ? lowerExpr(ctx, *expr.left) : emitConstI32(ctx, 0);
    switch (expr.unaryOp) {
    case ast::UnaryOp::Plus:
        return operand; // identity; no instruction needed
    case ast::UnaryOp::Minus:
        return emitUnaryOp(ctx, irs::Op::Neg, operand, toIrType(expr.type));
    case ast::UnaryOp::Not:
        return emitUnaryOp(ctx, irs::Op::Not, operand, toIrType(expr.type));
    }
    return operand;
}

irs::ValueId lowerBinary(LowerCtx &ctx, const ast::Expr &expr) {
    // A Real-typed operator needs Real operands: Pascal `/` is real division even when both
    // operands are integers, but Gemini's `DIV` truncates when it sees two ints.
    const irs::IrType resultType = toIrType(expr.type);
    const irs::ValueId left =
        expr.left ? lowerExprFor(ctx, *expr.left, resultType) : emitConstI32(ctx, 0);
    const irs::ValueId right =
        expr.right ? lowerExprFor(ctx, *expr.right, resultType) : emitConstI32(ctx, 0);
    return emitBinaryOp(ctx, toBinaryOp(expr.binaryOp), left, right, resultType);
}

irs::ValueId lowerCallExpr(LowerCtx &ctx, const ast::Expr &expr) {
    // Builtins are statement-only per M3 (never valid as an expression), so a Call
    // Expr here is always a user function call.
    std::vector<std::string> matCopies;
    return lowerUserCall(ctx, expr.text,
                         lowerCallArgs(ctx, expr.text, expr.args, &matCopies), toIrType(expr.type),
                         std::move(matCopies));
}

irs::ValueId lowerIndexExpr(LowerCtx &ctx, const ast::Expr &expr) {
    if (!expr.left) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "IR lowering: indexed expression missing base");
        return emitConstI32(ctx, 0);
    }
    const auto slot = resolveArrayBaseSlot(ctx, *expr.left);
    if (!slot) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "IR lowering: array base has no storage slot");
        return emitConstI32(ctx, 0);
    }
    const irs::ValueId index =
        expr.right ? lowerExpr(ctx, *expr.right) : emitConstI32(ctx, 0);
    return emitLoadIndex(ctx, slot->operand, index, arrayElementType(ctx, slot->operand));
}

irs::ValueId lowerSelectExpr(LowerCtx &ctx, const ast::Expr &expr) {
    if (!expr.left || expr.left->kind != ast::ExprKind::Identifier) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "IR lowering: field selection base must be a variable");
        return emitConstI32(ctx, 0);
    }
    const auto slot = resolveFieldSlot(ctx, expr.left->text, expr.text);
    if (!slot) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "IR lowering: record field '" + expr.text + "' has no slot");
        return emitConstI32(ctx, 0);
    }
    return emitLoad(ctx, slot->operand, slot->type);
}

irs::ValueId lowerExpr(LowerCtx &ctx, const ast::Expr &expr) {
    irs::ValueId result{};
    switch (expr.kind) {
    case ast::ExprKind::IntegerLiteral:
        result = emitConstI32(ctx, parseIntegerLiteral(expr.text));
        break;
    case ast::ExprKind::RealLiteral:
        result = emitConstF64(ctx, parseRealLiteral(expr.text));
        break;
    case ast::ExprKind::CharLiteral: {
        const std::string decoded = decodeQuotedLexeme(expr.text);
        result = emitConstChar(ctx, decoded.empty() ? '\0' : decoded.front());
        break;
    }
    case ast::ExprKind::StringLiteral:
        result = emitConstString(ctx, decodeQuotedLexeme(expr.text));
        break;
    case ast::ExprKind::Identifier:
        result = lowerIdentifier(ctx, expr);
        break;
    case ast::ExprKind::Unary:
        result = lowerUnary(ctx, expr);
        break;
    case ast::ExprKind::Binary:
        result = lowerBinary(ctx, expr);
        break;
    case ast::ExprKind::Call:
        result = lowerCallExpr(ctx, expr);
        break;
    case ast::ExprKind::Group:
        result = expr.left ? lowerExpr(ctx, *expr.left) : emitConstI32(ctx, 0);
        break;
    case ast::ExprKind::Index:
        result = lowerIndexExpr(ctx, expr);
        break;
    case ast::ExprKind::Select:
        result = lowerSelectExpr(ctx, expr);
        break;
    }
    return result;
}

void lowerWriteCall(LowerCtx &ctx, const std::string &foldedName,
                    const std::vector<std::unique_ptr<ast::Expr>> &args) {
    std::vector<irs::ValueId> argValues;
    argValues.reserve(args.size());
    for (const auto &arg : args) {
        if (arg) {
            argValues.push_back(lowerExpr(ctx, *arg));
        }
    }
    irs::Instr call;
    call.op = irs::Op::CallRuntime;
    call.type = irs::IrType::Void;
    call.text = foldedName;
    call.args = std::move(argValues);
    ctx.block.body.push_back(std::move(call));
}

void lowerReadCall(LowerCtx &ctx, const std::string &foldedName,
                   const std::vector<std::unique_ptr<ast::Expr>> &args) {
    for (const auto &arg : args) {
        if (!arg) {
            continue;
        }
        const std::string argFolded = foldAsciiLower(arg->text);
        const auto slot = resolveSlot(ctx, argFolded);
        if (!slot) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, arg->range.begin,
                                   "IR lowering: '" + arg->text + "' has no IR slot for '" +
                                       foldedName + "'");
            continue;
        }

        irs::Instr call;
        call.op = irs::Op::CallRuntime;
        call.type = slot->type;
        call.result = ctx.function.newTemp();
        call.text = foldedName;
        const irs::ValueId readValue = call.result;
        ctx.block.body.push_back(std::move(call));

        emitStore(ctx, slot->operand, readValue);
    }
}

void lowerCallStmt(LowerCtx &ctx, const ast::Stmt &stmt) {
    const std::string folded = foldAsciiLower(stmt.name);
    if (folded == "write" || folded == "writeln") {
        lowerWriteCall(ctx, folded, stmt.args);
        return;
    }
    if (folded == "read" || folded == "readln") {
        lowerReadCall(ctx, folded, stmt.args);
        return;
    }

    std::vector<std::string> matCopies;
    std::vector<irs::ValueId> argValues =
        lowerCallArgs(ctx, stmt.name, stmt.args, &matCopies);

    irs::IrType calleeReturnType = irs::IrType::Void;
    if (const Symbol *callee = ctx.symbols.lookup(stmt.name);
        callee && callee->kind == SymbolKind::Function) {
        calleeReturnType = toIrType(callee->type);
    }
    (void)lowerUserCall(ctx, stmt.name, std::move(argValues), calleeReturnType,
                        std::move(matCopies));
}

void lowerAssign(LowerCtx &ctx, const ast::Stmt &stmt) {
    const std::string folded = foldAsciiLower(stmt.name);
    // The destination type decides whether the RHS needs widening, so resolve it first.
    auto lowerValue = [&](irs::IrType target) {
        return stmt.value ? lowerExprFor(ctx, *stmt.value, target) : emitConstI32(ctx, 0);
    };

    if (ctx.functionResultName && folded == *ctx.functionResultName) {
        // Assigning to the enclosing function's own name sets its return value; there is
        // no local slot backing it.
        ctx.resultValue = lowerValue(ctx.function.returnType);
        return;
    }

    if (!stmt.fieldName.empty()) {
        const auto destSlot = resolveFieldSlot(ctx, stmt.name, stmt.fieldName);
        if (!destSlot) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                                   "IR lowering: record field '" + stmt.fieldName +
                                       "' has no slot");
            return;
        }
        if (stmt.index) {
            const irs::ValueId value = lowerValue(arrayElementType(ctx, destSlot->operand));
            const irs::ValueId index = lowerExpr(ctx, *stmt.index);
            emitStoreIndex(ctx, destSlot->operand, index, value);
            return;
        }
        emitStore(ctx, destSlot->operand, lowerValue(destSlot->type));
        return;
    }

    const auto recordIt = ctx.recordVars.find(folded);
    if (recordIt != ctx.recordVars.end()) {
        if (!stmt.index && stmt.value && stmt.value->kind == ast::ExprKind::Identifier) {
            copyRecordFields(ctx, stmt.name, stmt.value->text, recordIt->second);
            return;
        }
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "IR lowering: whole-record assignment requires a record "
                               "variable on the right-hand side");
        return;
    }

    const auto slot = resolveSlot(ctx, folded);
    if (!slot) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "IR lowering: assignment target '" + stmt.name +
                                   "' has no IR local slot");
        return;
    }
    if (stmt.index) {
        const irs::ValueId value = lowerValue(arrayElementType(ctx, slot->operand));
        const irs::ValueId index = lowerExpr(ctx, *stmt.index);
        emitStoreIndex(ctx, slot->operand, index, value);
        return;
    }
    if (slot->type == irs::IrType::ArrayRef && stmt.value &&
        stmt.value->kind == ast::ExprKind::Identifier) {
        const auto srcSlot = resolveSlot(ctx, foldAsciiLower(stmt.value->text));
        if (!srcSlot || srcSlot->type != irs::IrType::ArrayRef) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                                   "IR lowering: array copy requires an array variable source");
            return;
        }
        emitArrayCopy(ctx, slot->operand, srcSlot->operand);
        return;
    }
    emitStore(ctx, slot->operand, lowerValue(slot->type));
}

void lowerIf(LowerCtx &ctx, const ast::Stmt &stmt) {
    const irs::ValueId cond = stmt.condition ? lowerExpr(ctx, *stmt.condition) : emitConstBool(ctx, false);
    const std::string thenLabel = newLabel(ctx, "if.then.");
    const std::string endLabel = newLabel(ctx, "if.end.");
    const bool hasElse = static_cast<bool>(stmt.elseBranch);
    const std::string elseLabel = hasElse ? newLabel(ctx, "if.else.") : std::string();

    sealBlock(ctx, branchIfTo(cond, thenLabel, hasElse ? elseLabel : endLabel));

    beginBlock(ctx, thenLabel);
    if (stmt.thenBranch) {
        lowerStmt(ctx, *stmt.thenBranch);
    }
    sealBlock(ctx, branchTo(endLabel));

    if (hasElse) {
        beginBlock(ctx, elseLabel);
        lowerStmt(ctx, *stmt.elseBranch);
        sealBlock(ctx, branchTo(endLabel));
    }

    beginBlock(ctx, endLabel);
}

void lowerWhile(LowerCtx &ctx, const ast::Stmt &stmt) {
    const std::string headLabel = newLabel(ctx, "while.head.");
    const std::string bodyLabel = newLabel(ctx, "while.body.");
    const std::string endLabel = newLabel(ctx, "while.end.");

    sealBlock(ctx, branchTo(headLabel));

    beginBlock(ctx, headLabel);
    const irs::ValueId cond = stmt.condition ? lowerExpr(ctx, *stmt.condition) : emitConstBool(ctx, false);
    sealBlock(ctx, branchIfTo(cond, bodyLabel, endLabel));

    beginBlock(ctx, bodyLabel);
    if (stmt.thenBranch) {
        lowerStmt(ctx, *stmt.thenBranch);
    }
    sealBlock(ctx, branchTo(headLabel));

    beginBlock(ctx, endLabel);
}

void lowerRepeat(LowerCtx &ctx, const ast::Stmt &stmt) {
    const std::string bodyLabel = newLabel(ctx, "repeat.body.");
    const std::string endLabel = newLabel(ctx, "repeat.end.");

    sealBlock(ctx, branchTo(bodyLabel));

    beginBlock(ctx, bodyLabel);
    for (const auto &inner : stmt.statements) {
        lowerStmt(ctx, inner);
    }
    // `repeat ... until cond` loops while `cond` is false.
    const irs::ValueId cond = stmt.condition ? lowerExpr(ctx, *stmt.condition) : emitConstBool(ctx, true);
    sealBlock(ctx, branchIfTo(cond, endLabel, bodyLabel));

    beginBlock(ctx, endLabel);
}

void lowerFor(LowerCtx &ctx, const ast::Stmt &stmt) {
    const std::string folded = foldAsciiLower(stmt.name);
    const auto controlSlot = resolveSlot(ctx, folded);
    if (!controlSlot) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "IR lowering: for-loop control variable '" + stmt.name +
                                   "' has no IR local slot");
        return;
    }

    const irs::ValueId startValue = stmt.value ? lowerExpr(ctx, *stmt.value) : emitConstI32(ctx, 0);
    emitStore(ctx, controlSlot->operand, startValue);
    // The limit is evaluated once, before the loop, matching Pascal `for` semantics.
    const irs::ValueId limitValue =
        stmt.forLimit ? lowerExpr(ctx, *stmt.forLimit) : emitConstI32(ctx, 0);

    const std::string headLabel = newLabel(ctx, "for.head.");
    const std::string bodyLabel = newLabel(ctx, "for.body.");
    const std::string endLabel = newLabel(ctx, "for.end.");

    sealBlock(ctx, branchTo(headLabel));

    beginBlock(ctx, headLabel);
    const irs::ValueId current = emitLoad(ctx, controlSlot->operand, controlSlot->type);
    const irs::Op cmpOp = stmt.forDownto ? irs::Op::CmpGe : irs::Op::CmpLe;
    const irs::ValueId cond = emitBinaryOp(ctx, cmpOp, current, limitValue, irs::IrType::Bool);
    sealBlock(ctx, branchIfTo(cond, bodyLabel, endLabel));

    beginBlock(ctx, bodyLabel);
    if (stmt.thenBranch) {
        lowerStmt(ctx, *stmt.thenBranch);
    }
    const irs::ValueId currentAfterBody = emitLoad(ctx, controlSlot->operand, controlSlot->type);
    const irs::ValueId one = emitConstI32(ctx, 1);
    const irs::Op stepOp = stmt.forDownto ? irs::Op::Sub : irs::Op::Add;
    const irs::ValueId next =
        emitBinaryOp(ctx, stepOp, currentAfterBody, one, controlSlot->type);
    emitStore(ctx, controlSlot->operand, next);
    sealBlock(ctx, branchTo(headLabel));

    beginBlock(ctx, endLabel);
}

void lowerStmt(LowerCtx &ctx, const ast::Stmt &stmt) {
    switch (stmt.kind) {
    case ast::StmtKind::Compound:
        for (const auto &inner : stmt.statements) {
            lowerStmt(ctx, inner);
        }
        break;
    case ast::StmtKind::Assign:
        lowerAssign(ctx, stmt);
        break;
    case ast::StmtKind::Call:
        lowerCallStmt(ctx, stmt);
        break;
    case ast::StmtKind::If:
        lowerIf(ctx, stmt);
        break;
    case ast::StmtKind::While:
        lowerWhile(ctx, stmt);
        break;
    case ast::StmtKind::Repeat:
        lowerRepeat(ctx, stmt);
        break;
    case ast::StmtKind::For:
        lowerFor(ctx, stmt);
        break;
    }
}

void lowerLocalsAndConsts(LowerCtx &ctx, const ast::Block &block) {
    for (const auto &decl : block.consts) {
        ctx.constExprs.emplace(foldAsciiLower(decl.name), decl.value.get());
    }
    for (const auto &decl : block.vars) {
        const TypePtr localType = denoterType(ctx, decl.type);
        const TypePtr peeled = peelTypeAliases(localType);
        for (const auto &name : decl.names) {
            if (peeled && peeled->tag == TypeTag::Record) {
                ctx.recordVars.emplace(foldAsciiLower(name), localType);
                declareRecordFields(ctx, name, localType);
                continue;
            }
            const irs::IrType irLocalType = toIrType(localType);
            const auto slot = static_cast<std::uint32_t>(ctx.function.locals.size());
            irs::Local local{name, irLocalType};
            fillArrayMeta(local, localType);
            ctx.function.locals.push_back(std::move(local));
            ctx.localSlots.emplace(foldAsciiLower(name), slot);
        }
    }
}

void emitArrayDims(LowerCtx &ctx) {
    for (std::uint32_t i = 0; i < ctx.function.params.size(); ++i) {
        const irs::Param &param = ctx.function.params[i];
        if (param.type != irs::IrType::ArrayRef || !param.arrayLow || !param.arrayHigh) {
            continue;
        }
        const std::int64_t size = *param.arrayHigh - *param.arrayLow + 1;
        if (size < 1) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                   apollo::common::SourceLocation{},
                                   "IR lowering: array parameter '" + param.name +
                                       "' has an empty range");
            continue;
        }
        emitDimArray(ctx, makeParamOperand(i), size, param.arrayElement);
    }
    for (std::uint32_t i = 0; i < ctx.function.locals.size(); ++i) {
        const irs::Local &local = ctx.function.locals[i];
        if (local.type != irs::IrType::ArrayRef || !local.arrayLow || !local.arrayHigh) {
            continue;
        }
        const std::int64_t size = *local.arrayHigh - *local.arrayLow + 1;
        if (size < 1) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                   apollo::common::SourceLocation{},
                                   "IR lowering: array '" + local.name + "' has an empty range");
            continue;
        }
        emitDimArray(ctx, makeLocalOperand(i), size, local.arrayElement);
    }
}

irs::Function lowerFunctionCore(const SymbolTable &symbols,
                                apollo::common::DiagnosticEngine &diagnostics,
                                std::string functionName, irs::IrType returnType,
                                const std::vector<ast::ParamDecl> *params,
                                const ast::Block &block,
                                std::optional<std::string> resultName) {
    irs::Function function;
    function.name = std::move(functionName);
    function.returnType = returnType;

    LowerCtx ctx{symbols,     diagnostics,
                 function,
                 irs::BasicBlock{"entry"},
                 {},
                 {},
                 {},
                 {},
                 {},
                 0,
                 resultName,
                 {}};

    if (params) {
        for (const auto &param : *params) {
            const TypePtr paramType = denoterType(ctx, param.type);
            const irs::IrType irParamType = toIrType(paramType);
            for (const auto &name : param.names) {
                const auto slot = static_cast<std::uint32_t>(function.params.size());
                irs::Param p{name, irParamType};
                fillArrayMeta(p, paramType);
                function.params.push_back(std::move(p));
                ctx.paramSlots.emplace(foldAsciiLower(name), slot);
            }
        }
    }

    lowerLocalsAndConsts(ctx, block);
    emitArrayDims(ctx);

    if (resultName) {
        ctx.resultValue = emitZeroValue(ctx, returnType);
    }

    for (const auto &stmt : block.body.statements) {
        lowerStmt(ctx, stmt);
    }

    irs::Terminator ret;
    ret.kind = irs::TerminatorKind::Return;
    if (resultName) {
        ret.value = ctx.resultValue;
    }
    sealBlock(ctx, std::move(ret));

    return function;
}

} // namespace

apollo::ir::Module lowerToIr(const ast::Program &program, const SymbolTable &symbols,
                             apollo::common::DiagnosticEngine &diagnostics) {
    irs::Module module;
    module.name = program.name;

    module.functions.push_back(lowerFunctionCore(symbols, diagnostics, "main", irs::IrType::Void,
                                                 /*params=*/nullptr, program.block,
                                                 /*resultName=*/std::nullopt));

    // Only subprograms declared directly in the program block are lowered (one level);
    // deeper nesting is diagnosed rather than silently skipped (see Stage 3 notes).
    for (const auto &sub : program.block.subprograms) {
        if (!sub.block) {
            continue;
        }
        const Symbol *subSymbol = symbols.lookup(sub.name);
        const irs::IrType returnType =
            sub.isFunction && subSymbol ? toIrType(subSymbol->type) : irs::IrType::Void;
        const std::optional<std::string> resultName =
            sub.isFunction ? std::optional<std::string>(foldAsciiLower(sub.name)) : std::nullopt;

        module.functions.push_back(lowerFunctionCore(symbols, diagnostics, sub.name, returnType,
                                                     &sub.params, *sub.block, resultName));

        if (!sub.block->subprograms.empty()) {
            diagnostics.report(apollo::common::DiagnosticSeverity::Error, sub.range.begin,
                               "nested subprogram lowering not supported until a later stage");
        }
    }

    return module;
}

} // namespace apollo::pascal::ir
