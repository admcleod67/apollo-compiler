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
    case TypeTag::Text:
    case TypeTag::File:
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
    /// File / text variables: folded name -> Text or File type (slots are mangled locals).
    std::unordered_map<std::string, TypePtr> fileVars;
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

void declareLocalNamed(LowerCtx &ctx, const std::string &name, irs::IrType type) {
    const auto slot = static_cast<std::uint32_t>(ctx.function.locals.size());
    ctx.function.locals.push_back(irs::Local{name, type});
    ctx.localSlots.emplace(foldAsciiLower(name), slot);
}

void declareFileSlots(LowerCtx &ctx, std::string_view baseName, const TypePtr &fileType) {
    const TypePtr peeled = peelTypeAliases(fileType);
    if (!peeled || (peeled->tag != TypeTag::Text && peeled->tag != TypeTag::File)) {
        return;
    }
    const std::string base(baseName);
    declareLocalNamed(ctx, base + "$path", irs::IrType::StringRef);
    declareLocalNamed(ctx, base + "$h", irs::IrType::I32);
    declareLocalNamed(ctx, base + "$rd", irs::IrType::I32);
    declareLocalNamed(ctx, base + "$eof", irs::IrType::I32);
    if (peeled->tag == TypeTag::Text) {
        declareLocalNamed(ctx, base + "$peek", irs::IrType::StringRef);
        declareLocalNamed(ctx, base + "$eoln", irs::IrType::I32);
    }
    ctx.fileVars.emplace(foldAsciiLower(baseName), fileType);
}

std::optional<ResolvedSlot> resolveFileSlot(const LowerCtx &ctx, std::string_view baseName,
                                            std::string_view suffix) {
    return resolveSlot(ctx, foldAsciiLower(baseName) + "$" + foldAsciiLower(suffix));
}

bool isFileVarName(const LowerCtx &ctx, std::string_view folded) {
    return ctx.fileVars.find(std::string(folded)) != ctx.fileVars.end();
}

TypePtr fileVarType(const LowerCtx &ctx, std::string_view folded) {
    const auto it = ctx.fileVars.find(std::string(folded));
    if (it == ctx.fileVars.end()) {
        return nullptr;
    }
    return peelTypeAliases(it->second);
}

irs::ValueId emitRuntimeCall(LowerCtx &ctx, std::string name, irs::IrType resultType,
                             std::vector<irs::ValueId> args) {
    irs::Instr call;
    call.op = irs::Op::CallRuntime;
    call.type = resultType;
    call.text = std::move(name);
    call.args = std::move(args);
    if (resultType != irs::IrType::Void) {
        call.result = ctx.function.newTemp();
    }
    const irs::ValueId result = call.result;
    ctx.block.body.push_back(std::move(call));
    return result;
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
std::string newLabel(LowerCtx &ctx, const char *prefix);
void sealBlock(LowerCtx &ctx, irs::Terminator term);
void beginBlock(LowerCtx &ctx, std::string label);
irs::Terminator branchTo(std::string target);
irs::Terminator branchIfTo(irs::ValueId cond, std::string trueTarget, std::string falseTarget);

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
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                   apollo::common::SourceLocation{},
                                   "IR lowering: missing record field slot for '" + field.name +
                                       "'");
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
/// Value array args become call-site dim/init/copy setups (not stack values).
std::vector<irs::ValueId> lowerCallArgs(LowerCtx &ctx, const std::string &calleeName,
                                        const std::vector<std::unique_ptr<ast::Expr>> &args,
                                        std::vector<irs::ArrayCopySetup> *matCopies) {
    const Symbol *callee = ctx.symbols.lookup(calleeName);
    const std::string calleeDeclared =
        callee ? callee->name : calleeName;
    std::vector<irs::ValueId> values;
    values.reserve(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (!args[i]) {
            continue;
        }
        irs::IrType paramType = irs::IrType::Error;
        bool isVar = false;
        TypePtr paramPascalType;
        if (callee && i < callee->paramTypes.size()) {
            paramPascalType = callee->paramTypes[i];
            paramType = toIrType(paramPascalType);
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
                const auto actualSlot = resolveSlot(ctx, foldAsciiLower(args[i]->text));
                std::string actualName = args[i]->text;
                if (actualSlot && actualSlot->operand.kind == irs::OperandKind::Local &&
                    actualSlot->operand.slot < ctx.function.locals.size()) {
                    actualName = ctx.function.locals[actualSlot->operand.slot].name;
                } else if (actualSlot && actualSlot->operand.kind == irs::OperandKind::Param &&
                           actualSlot->operand.slot < ctx.function.params.size()) {
                    actualName = ctx.function.params[actualSlot->operand.slot].name;
                }
                TypePtr peeled = peelTypeAliases(paramPascalType);
                std::int64_t size = 0;
                irs::IrType element = irs::IrType::I32;
                if (peeled && peeled->tag == TypeTag::Array && peeled->hasBounds) {
                    size = peeled->indexHigh - peeled->indexLow + 1;
                    element = toIrType(peeled->element);
                }
                if (size < 1) {
                    ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                                           args[i]->range.begin,
                                           "IR lowering: array parameter has an empty range");
                    continue;
                }
                irs::ArrayCopySetup setup;
                setup.dst = fieldStorageName(calleeDeclared, callee->paramNames[i]);
                setup.src = fieldStorageName(ctx.function.name, actualName);
                setup.size = size;
                setup.element = element;
                matCopies->push_back(std::move(setup));
            }
            continue;
        }
        values.push_back(lowerExprFor(ctx, *args[i], paramType));
    }
    return values;
}

irs::ValueId lowerUserCall(LowerCtx &ctx, const std::string &calleeName,
                           std::vector<irs::ValueId> args, irs::IrType resultType,
                           std::vector<irs::ArrayCopySetup> matCopies) {
    const Symbol *callee = ctx.symbols.lookup(calleeName);
    irs::Instr call;
    call.op = irs::Op::Call;
    call.type = resultType;
    if (resultType != irs::IrType::Void) {
        call.result = ctx.function.newTemp();
    }
    call.text = callee ? callee->name : calleeName;
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

irs::Operand declareTempLocal(LowerCtx &ctx, const char *prefix, irs::IrType type) {
    const std::string name = std::string(prefix) + std::to_string(ctx.blockCounter++);
    const auto slot = static_cast<std::uint32_t>(ctx.function.locals.size());
    ctx.function.locals.push_back(irs::Local{name, type});
    return makeLocalOperand(slot);
}

/// Half away from zero: add ±0.5 by sign, then ConvertI32.
irs::ValueId lowerRound(LowerCtx &ctx, irs::ValueId value) {
    const irs::Operand dest = declareTempLocal(ctx, "$round.", irs::IrType::I32);
    const irs::ValueId zero = emitConstF64(ctx, 0.0);
    const irs::ValueId ge =
        emitBinaryOp(ctx, irs::Op::CmpGe, value, zero, irs::IrType::Bool);
    const std::string posLabel = newLabel(ctx, "round.pos.");
    const std::string negLabel = newLabel(ctx, "round.neg.");
    const std::string mergeLabel = newLabel(ctx, "round.merge.");

    sealBlock(ctx, branchIfTo(ge, posLabel, negLabel));

    beginBlock(ctx, posLabel);
    {
        const irs::ValueId half = emitConstF64(ctx, 0.5);
        const irs::ValueId sum = emitBinaryOp(ctx, irs::Op::Add, value, half, irs::IrType::F64);
        const irs::ValueId truncated =
            emitUnaryOp(ctx, irs::Op::ConvertI32, sum, irs::IrType::I32);
        emitStore(ctx, dest, truncated);
        sealBlock(ctx, branchTo(mergeLabel));
    }

    beginBlock(ctx, negLabel);
    {
        const irs::ValueId half = emitConstF64(ctx, -0.5);
        const irs::ValueId sum = emitBinaryOp(ctx, irs::Op::Add, value, half, irs::IrType::F64);
        const irs::ValueId truncated =
            emitUnaryOp(ctx, irs::Op::ConvertI32, sum, irs::IrType::I32);
        emitStore(ctx, dest, truncated);
        sealBlock(ctx, branchTo(mergeLabel));
    }

    beginBlock(ctx, mergeLabel);
    return emitLoad(ctx, dest, irs::IrType::I32);
}

irs::ValueId lowerAbsReal(LowerCtx &ctx, irs::ValueId value) {
    const irs::Operand dest = declareTempLocal(ctx, "$abs.", irs::IrType::F64);
    const irs::ValueId zero = emitConstF64(ctx, 0.0);
    const irs::ValueId lt =
        emitBinaryOp(ctx, irs::Op::CmpLt, value, zero, irs::IrType::Bool);
    const std::string negLabel = newLabel(ctx, "abs.neg.");
    const std::string posLabel = newLabel(ctx, "abs.pos.");
    const std::string mergeLabel = newLabel(ctx, "abs.merge.");

    sealBlock(ctx, branchIfTo(lt, negLabel, posLabel));

    beginBlock(ctx, negLabel);
    {
        const irs::ValueId negated = emitUnaryOp(ctx, irs::Op::Neg, value, irs::IrType::F64);
        emitStore(ctx, dest, negated);
        sealBlock(ctx, branchTo(mergeLabel));
    }

    beginBlock(ctx, posLabel);
    {
        emitStore(ctx, dest, value);
        sealBlock(ctx, branchTo(mergeLabel));
    }

    beginBlock(ctx, mergeLabel);
    return emitLoad(ctx, dest, irs::IrType::F64);
}

irs::ValueId lowerStdFuncCall(LowerCtx &ctx, const ast::Expr &expr) {
    const std::string folded = foldAsciiLower(expr.text);
    const irs::IrType resultType = toIrType(expr.type);
    const irs::ValueId arg =
        !expr.args.empty() && expr.args[0] ? lowerExpr(ctx, *expr.args[0]) : emitConstI32(ctx, 0);

    if (folded == "ord") {
        return emitUnaryOp(ctx, irs::Op::Copy, arg, irs::IrType::I32);
    }
    if (folded == "chr") {
        return emitUnaryOp(ctx, irs::Op::Copy, arg, irs::IrType::Char);
    }
    if (folded == "succ") {
        const irs::ValueId one = emitConstI32(ctx, 1);
        return emitBinaryOp(ctx, irs::Op::Add, arg, one, resultType);
    }
    if (folded == "pred") {
        const irs::ValueId one = emitConstI32(ctx, 1);
        return emitBinaryOp(ctx, irs::Op::Sub, arg, one, resultType);
    }
    if (folded == "odd") {
        const irs::ValueId two = emitConstI32(ctx, 2);
        const irs::ValueId rem = emitBinaryOp(ctx, irs::Op::Mod, arg, two, irs::IrType::I32);
        const irs::ValueId zero = emitConstI32(ctx, 0);
        return emitBinaryOp(ctx, irs::Op::CmpNe, rem, zero, irs::IrType::Bool);
    }
    if (folded == "sqr") {
        return emitBinaryOp(ctx, irs::Op::Mul, arg, arg, resultType);
    }
    if (folded == "abs") {
        if (resultType == irs::IrType::F64) {
            return lowerAbsReal(ctx, arg);
        }
        return emitUnaryOp(ctx, irs::Op::Abs, arg, irs::IrType::I32);
    }
    if (folded == "trunc") {
        return emitUnaryOp(ctx, irs::Op::ConvertI32, arg, irs::IrType::I32);
    }
    if (folded == "round") {
        return lowerRound(ctx, arg);
    }
    if (folded == "sqrt" || folded == "sin" || folded == "cos" || folded == "arctan" ||
        folded == "ln" || folded == "exp") {
        irs::ValueId realArg = arg;
        if (!expr.args.empty() && expr.args[0] && toIrType(expr.args[0]->type) == irs::IrType::I32) {
            realArg = emitUnaryOp(ctx, irs::Op::ConvertF64, arg, irs::IrType::F64);
        }
        irs::Instr call;
        call.op = irs::Op::CallRuntime;
        call.type = irs::IrType::F64;
        call.result = ctx.function.newTemp();
        call.text = folded;
        call.args = {realArg};
        const irs::ValueId result = call.result;
        ctx.block.body.push_back(std::move(call));
        return result;
    }

    ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                           "IR lowering: unsupported standard function '" + expr.text + "'");
    return emitConstI32(ctx, 0);
}

bool isStdFuncName(std::string_view folded) {
    return folded == "ord" || folded == "chr" || folded == "succ" || folded == "pred" ||
           folded == "odd" || folded == "abs" || folded == "sqr" || folded == "trunc" ||
           folded == "round" || folded == "sqrt" || folded == "sin" || folded == "cos" ||
           folded == "arctan" || folded == "ln" || folded == "exp";
}

irs::ValueId lowerEofExpr(LowerCtx &ctx, const ast::Expr &expr);
irs::ValueId lowerEolnExpr(LowerCtx &ctx, const ast::Expr &expr);

irs::ValueId lowerCallExpr(LowerCtx &ctx, const ast::Expr &expr) {
    const std::string folded = foldAsciiLower(expr.text);
    if (isStdFuncName(folded)) {
        return lowerStdFuncCall(ctx, expr);
    }
    if (folded == "eof") {
        return lowerEofExpr(ctx, expr);
    }
    if (folded == "eoln") {
        return lowerEolnExpr(ctx, expr);
    }
    // User function call (console builtins are statement-only).
    std::vector<irs::ArrayCopySetup> matCopies;
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

irs::ValueId loadFileSlot(LowerCtx &ctx, std::string_view fileName, std::string_view suffix,
                          irs::IrType type) {
    const auto slot = resolveFileSlot(ctx, fileName, suffix);
    if (!slot) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                               apollo::common::SourceLocation{},
                               "IR lowering: missing file slot '" + std::string(fileName) + "$" +
                                   std::string(suffix) + "'");
        return emitZeroValue(ctx, type);
    }
    return emitLoad(ctx, slot->operand, type);
}

void storeFileSlot(LowerCtx &ctx, std::string_view fileName, std::string_view suffix,
                   irs::ValueId value) {
    const auto slot = resolveFileSlot(ctx, fileName, suffix);
    if (!slot) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error,
                               apollo::common::SourceLocation{},
                               "IR lowering: missing file slot '" + std::string(fileName) + "$" +
                                   std::string(suffix) + "'");
        return;
    }
    emitStore(ctx, slot->operand, value);
}

irs::ValueId lowerToString(LowerCtx &ctx, const ast::Expr &expr) {
    const TypeTag tag = canonicalTag(expr.type);
    if (tag == TypeTag::String) {
        return lowerExpr(ctx, expr);
    }
    if (tag == TypeTag::Char) {
        const irs::ValueId code = lowerExpr(ctx, expr);
        return emitRuntimeCall(ctx, "str.fromByte", irs::IrType::StringRef, {code});
    }
    if (tag == TypeTag::Boolean) {
        const irs::ValueId v = lowerExpr(ctx, expr);
        const irs::Operand dest = declareTempLocal(ctx, "$bstr.", irs::IrType::StringRef);
        const std::string tLabel = newLabel(ctx, "bstr.t.");
        const std::string fLabel = newLabel(ctx, "bstr.f.");
        const std::string mLabel = newLabel(ctx, "bstr.m.");
        sealBlock(ctx, branchIfTo(v, tLabel, fLabel));
        beginBlock(ctx, tLabel);
        emitStore(ctx, dest, emitConstString(ctx, "TRUE"));
        sealBlock(ctx, branchTo(mLabel));
        beginBlock(ctx, fLabel);
        emitStore(ctx, dest, emitConstString(ctx, "FALSE"));
        sealBlock(ctx, branchTo(mLabel));
        beginBlock(ctx, mLabel);
        return emitLoad(ctx, dest, irs::IrType::StringRef);
    }
    // integer / real → BASIC STR
    const irs::ValueId v = lowerExpr(ctx, expr);
    return emitRuntimeCall(ctx, "basic.str", irs::IrType::StringRef, {v});
}

void lowerCloseFile(LowerCtx &ctx, std::string_view fileName) {
    const irs::ValueId handle = loadFileSlot(ctx, fileName, "h", irs::IrType::I32);
    const irs::ValueId zero = emitConstI32(ctx, 0);
    const irs::ValueId open =
        emitBinaryOp(ctx, irs::Op::CmpNe, handle, zero, irs::IrType::Bool);
    const std::string doClose = newLabel(ctx, "fclose.");
    const std::string done = newLabel(ctx, "fclose.done.");
    sealBlock(ctx, branchIfTo(open, doClose, done));
    beginBlock(ctx, doClose);
    (void)emitRuntimeCall(ctx, "hostfs.close", irs::IrType::I32, {handle});
    storeFileSlot(ctx, fileName, "h", emitConstI32(ctx, 0));
    storeFileSlot(ctx, fileName, "rd", emitConstI32(ctx, 0));
    sealBlock(ctx, branchTo(done));
    beginBlock(ctx, done);
}

void lowerAssignFile(LowerCtx &ctx, const ast::Stmt &stmt) {
    if (stmt.args.size() < 2 || !stmt.args[0] || !stmt.args[1]) {
        return;
    }
    const std::string fileName = stmt.args[0]->text;
    const irs::ValueId path = lowerToString(ctx, *stmt.args[1]);
    storeFileSlot(ctx, fileName, "path", path);
}

void lowerResetRewrite(LowerCtx &ctx, const ast::Stmt &stmt, bool forRead) {
    if (stmt.args.empty() || !stmt.args[0]) {
        return;
    }
    const std::string fileName = stmt.args[0]->text;
    if (stmt.args.size() >= 2 && stmt.args[1]) {
        storeFileSlot(ctx, fileName, "path", lowerToString(ctx, *stmt.args[1]));
    }
    lowerCloseFile(ctx, fileName);
    const irs::ValueId path = loadFileSlot(ctx, fileName, "path", irs::IrType::StringRef);
    const irs::ValueId handle =
        emitRuntimeCall(ctx, forRead ? "hostfs.openRead" : "hostfs.openWrite", irs::IrType::I32,
                        {path});
    storeFileSlot(ctx, fileName, "h", handle);
    storeFileSlot(ctx, fileName, "rd", emitConstI32(ctx, forRead ? 1 : 0));
    storeFileSlot(ctx, fileName, "eof", emitConstI32(ctx, 0));
    if (fileVarType(ctx, foldAsciiLower(fileName)) &&
        canonicalTag(fileVarType(ctx, foldAsciiLower(fileName))) == TypeTag::Text) {
        storeFileSlot(ctx, fileName, "peek", emitConstString(ctx, ""));
        storeFileSlot(ctx, fileName, "eoln", emitConstI32(ctx, 0));
    }
}

/// Ensure text file has a 1-byte peek (or eof).
void lowerTextFillPeek(LowerCtx &ctx, std::string_view fileName) {
    const irs::ValueId peek = loadFileSlot(ctx, fileName, "peek", irs::IrType::StringRef);
    const irs::ValueId empty = emitConstString(ctx, "");
    const irs::ValueId isEmpty =
        emitBinaryOp(ctx, irs::Op::CmpEq, peek, empty, irs::IrType::Bool);
    const std::string need = newLabel(ctx, "tpeek.");
    const std::string done = newLabel(ctx, "tpeek.done.");
    sealBlock(ctx, branchIfTo(isEmpty, need, done));
    beginBlock(ctx, need);
    {
        const irs::ValueId eofFlag = loadFileSlot(ctx, fileName, "eof", irs::IrType::I32);
        const irs::ValueId alreadyEof =
            emitBinaryOp(ctx, irs::Op::CmpNe, eofFlag, emitConstI32(ctx, 0), irs::IrType::Bool);
        const std::string readLab = newLabel(ctx, "tpeek.read.");
        const std::string skipLab = newLabel(ctx, "tpeek.skip.");
        sealBlock(ctx, branchIfTo(alreadyEof, skipLab, readLab));
        beginBlock(ctx, readLab);
        {
            const irs::ValueId handle = loadFileSlot(ctx, fileName, "h", irs::IrType::I32);
            const irs::ValueId one = emitConstI32(ctx, 1);
            const irs::ValueId chunk =
                emitRuntimeCall(ctx, "hostfs.read", irs::IrType::StringRef, {handle, one});
            const irs::ValueId gotEmpty =
                emitBinaryOp(ctx, irs::Op::CmpEq, chunk, empty, irs::IrType::Bool);
            const std::string atEof = newLabel(ctx, "tpeek.eof.");
            const std::string have = newLabel(ctx, "tpeek.have.");
            sealBlock(ctx, branchIfTo(gotEmpty, atEof, have));
            beginBlock(ctx, atEof);
            storeFileSlot(ctx, fileName, "eof", emitConstI32(ctx, 1));
            storeFileSlot(ctx, fileName, "eoln", emitConstI32(ctx, 1));
            sealBlock(ctx, branchTo(skipLab));
            beginBlock(ctx, have);
            storeFileSlot(ctx, fileName, "peek", chunk);
            const irs::ValueId code =
                emitRuntimeCall(ctx, "str.seq", irs::IrType::I32, {chunk});
            const irs::ValueId nl = emitConstI32(ctx, static_cast<unsigned char>('\n'));
            const irs::ValueId isNl =
                emitBinaryOp(ctx, irs::Op::CmpEq, code, nl, irs::IrType::Bool);
            // Store eoln as 0/1 int from bool.
            storeFileSlot(ctx, fileName, "eoln",
                          emitUnaryOp(ctx, irs::Op::Copy, isNl, irs::IrType::I32));
            sealBlock(ctx, branchTo(skipLab));
        }
        beginBlock(ctx, skipLab);
        sealBlock(ctx, branchTo(done));
    }
    beginBlock(ctx, done);
}

irs::ValueId lowerEofExpr(LowerCtx &ctx, const ast::Expr &expr) {
    if (expr.args.empty() || !expr.args[0]) {
        return emitConstBool(ctx, true);
    }
    const std::string fileName = expr.args[0]->text;
    if (canonicalTag(expr.args[0]->type) == TypeTag::Text) {
        lowerTextFillPeek(ctx, fileName);
    }
    const irs::ValueId flag = loadFileSlot(ctx, fileName, "eof", irs::IrType::I32);
    return emitBinaryOp(ctx, irs::Op::CmpNe, flag, emitConstI32(ctx, 0), irs::IrType::Bool);
}

irs::ValueId lowerEolnExpr(LowerCtx &ctx, const ast::Expr &expr) {
    if (expr.args.empty() || !expr.args[0]) {
        return emitConstBool(ctx, true);
    }
    const std::string fileName = expr.args[0]->text;
    lowerTextFillPeek(ctx, fileName);
    const irs::ValueId flag = loadFileSlot(ctx, fileName, "eoln", irs::IrType::I32);
    return emitBinaryOp(ctx, irs::Op::CmpNe, flag, emitConstI32(ctx, 0), irs::IrType::Bool);
}

irs::ValueId lowerByteMod256(LowerCtx &ctx, irs::ValueId n) {
    const irs::ValueId m = emitConstI32(ctx, 256);
    const irs::ValueId rem = emitBinaryOp(ctx, irs::Op::Mod, n, m, irs::IrType::I32);
    const irs::ValueId zero = emitConstI32(ctx, 0);
    const irs::ValueId neg =
        emitBinaryOp(ctx, irs::Op::CmpLt, rem, zero, irs::IrType::Bool);
    const irs::Operand dest = declareTempLocal(ctx, "$b256.", irs::IrType::I32);
    const std::string adj = newLabel(ctx, "b256.adj.");
    const std::string ok = newLabel(ctx, "b256.ok.");
    const std::string merge = newLabel(ctx, "b256.m.");
    sealBlock(ctx, branchIfTo(neg, adj, ok));
    beginBlock(ctx, adj);
    emitStore(ctx, dest, emitBinaryOp(ctx, irs::Op::Add, rem, m, irs::IrType::I32));
    sealBlock(ctx, branchTo(merge));
    beginBlock(ctx, ok);
    emitStore(ctx, dest, rem);
    sealBlock(ctx, branchTo(merge));
    beginBlock(ctx, merge);
    return emitLoad(ctx, dest, irs::IrType::I32);
}

irs::ValueId lowerFloorDiv256(LowerCtx &ctx, irs::ValueId n) {
    const irs::ValueId m = emitConstI32(ctx, 256);
    const irs::ValueId q = emitBinaryOp(ctx, irs::Op::Div, n, m, irs::IrType::I32);
    const irs::ValueId r = emitBinaryOp(ctx, irs::Op::Mod, n, m, irs::IrType::I32);
    const irs::ValueId zero = emitConstI32(ctx, 0);
    const irs::ValueId nNeg =
        emitBinaryOp(ctx, irs::Op::CmpLt, n, zero, irs::IrType::Bool);
    const irs::ValueId rNonZero =
        emitBinaryOp(ctx, irs::Op::CmpNe, r, zero, irs::IrType::Bool);
    const irs::ValueId needAdj =
        emitBinaryOp(ctx, irs::Op::And, nNeg, rNonZero, irs::IrType::Bool);
    const irs::Operand dest = declareTempLocal(ctx, "$fd256.", irs::IrType::I32);
    const std::string adj = newLabel(ctx, "fd256.adj.");
    const std::string ok = newLabel(ctx, "fd256.ok.");
    const std::string merge = newLabel(ctx, "fd256.m.");
    sealBlock(ctx, branchIfTo(needAdj, adj, ok));
    beginBlock(ctx, adj);
    emitStore(ctx, dest,
              emitBinaryOp(ctx, irs::Op::Sub, q, emitConstI32(ctx, 1), irs::IrType::I32));
    sealBlock(ctx, branchTo(merge));
    beginBlock(ctx, ok);
    emitStore(ctx, dest, q);
    sealBlock(ctx, branchTo(merge));
    beginBlock(ctx, merge);
    return emitLoad(ctx, dest, irs::IrType::I32);
}

void lowerHostfsWriteByte(LowerCtx &ctx, irs::ValueId handle, irs::ValueId byteVal) {
    const irs::ValueId ch =
        emitRuntimeCall(ctx, "str.fromByte", irs::IrType::StringRef, {byteVal});
    (void)emitRuntimeCall(ctx, "hostfs.write", irs::IrType::I32, {handle, ch});
}

void lowerWriteI32LE(LowerCtx &ctx, irs::ValueId handle, irs::ValueId value) {
    irs::ValueId n = value;
    for (int i = 0; i < 4; ++i) {
        const irs::ValueId b = lowerByteMod256(ctx, n);
        lowerHostfsWriteByte(ctx, handle, b);
        n = lowerFloorDiv256(ctx, n);
    }
}

irs::ValueId lowerReadI32LE(LowerCtx &ctx, std::string_view fileName, irs::ValueId handle) {
    const irs::Operand dest = declareTempLocal(ctx, "$ri32.", irs::IrType::I32);
    emitStore(ctx, dest, emitConstI32(ctx, 0));
    irs::ValueId shift = emitConstI32(ctx, 1); // multiplier 256^i
    for (int i = 0; i < 4; ++i) {
        const irs::ValueId one = emitConstI32(ctx, 1);
        const irs::ValueId chunk =
            emitRuntimeCall(ctx, "hostfs.read", irs::IrType::StringRef, {handle, one});
        const irs::ValueId empty = emitConstString(ctx, "");
        const irs::ValueId gotEmpty =
            emitBinaryOp(ctx, irs::Op::CmpEq, chunk, empty, irs::IrType::Bool);
        const std::string atEof = newLabel(ctx, "ri32.eof.");
        const std::string have = newLabel(ctx, "ri32.ok.");
        const std::string cont = newLabel(ctx, "ri32.c.");
        sealBlock(ctx, branchIfTo(gotEmpty, atEof, have));
        beginBlock(ctx, atEof);
        storeFileSlot(ctx, fileName, "eof", emitConstI32(ctx, 1));
        sealBlock(ctx, branchTo(cont));
        beginBlock(ctx, have);
        {
            const irs::ValueId b =
                emitRuntimeCall(ctx, "str.seq", irs::IrType::I32, {chunk});
            const irs::ValueId contrib =
                emitBinaryOp(ctx, irs::Op::Mul, b, shift, irs::IrType::I32);
            const irs::ValueId cur = emitLoad(ctx, dest, irs::IrType::I32);
            emitStore(ctx, dest, emitBinaryOp(ctx, irs::Op::Add, cur, contrib, irs::IrType::I32));
            shift = emitBinaryOp(ctx, irs::Op::Mul, shift, emitConstI32(ctx, 256), irs::IrType::I32);
            sealBlock(ctx, branchTo(cont));
        }
        beginBlock(ctx, cont);
    }
    return emitLoad(ctx, dest, irs::IrType::I32);
}

void lowerWriteCharByte(LowerCtx &ctx, irs::ValueId handle, irs::ValueId code) {
    lowerHostfsWriteByte(ctx, handle, lowerByteMod256(ctx, code));
}

irs::ValueId lowerReadCharByte(LowerCtx &ctx, std::string_view fileName, irs::ValueId handle) {
    const irs::ValueId one = emitConstI32(ctx, 1);
    const irs::ValueId chunk =
        emitRuntimeCall(ctx, "hostfs.read", irs::IrType::StringRef, {handle, one});
    const irs::ValueId empty = emitConstString(ctx, "");
    const irs::ValueId gotEmpty =
        emitBinaryOp(ctx, irs::Op::CmpEq, chunk, empty, irs::IrType::Bool);
    const irs::Operand dest = declareTempLocal(ctx, "$rch.", irs::IrType::I32);
    const std::string atEof = newLabel(ctx, "rch.eof.");
    const std::string have = newLabel(ctx, "rch.ok.");
    const std::string merge = newLabel(ctx, "rch.m.");
    sealBlock(ctx, branchIfTo(gotEmpty, atEof, have));
    beginBlock(ctx, atEof);
    storeFileSlot(ctx, fileName, "eof", emitConstI32(ctx, 1));
    emitStore(ctx, dest, emitConstI32(ctx, 0));
    sealBlock(ctx, branchTo(merge));
    beginBlock(ctx, have);
    emitStore(ctx, dest, emitRuntimeCall(ctx, "str.seq", irs::IrType::I32, {chunk}));
    sealBlock(ctx, branchTo(merge));
    beginBlock(ctx, merge);
    return emitLoad(ctx, dest, irs::IrType::I32);
}

/// Stage 3: `real` typed components are 24-byte ASCII (`basic.str` / `coerce.flt`), not IEEE.
void lowerWriteRealAscii(LowerCtx &ctx, irs::ValueId handle, irs::ValueId value) {
    irs::ValueId s = emitRuntimeCall(ctx, "basic.str", irs::IrType::StringRef, {value});
    // Pad/truncate to 24 bytes with spaces (simple: write STR then pad with spaces to 24).
    const irs::Operand buf = declareTempLocal(ctx, "$rasc.", irs::IrType::StringRef);
    emitStore(ctx, buf, s);
    const irs::Operand len = declareTempLocal(ctx, "$rlen.", irs::IrType::I32);
    // LEN via BASIC CALL_FUNC 2,3,1 — reuse as runtime name basic.len
    {
        irs::Instr call;
        call.op = irs::Op::CallRuntime;
        call.type = irs::IrType::I32;
        call.result = ctx.function.newTemp();
        call.text = "basic.len";
        call.args = {emitLoad(ctx, buf, irs::IrType::StringRef)};
        const irs::ValueId L = call.result;
        ctx.block.body.push_back(std::move(call));
        emitStore(ctx, len, L);
    }
    const std::string padLoop = newLabel(ctx, "rpad.");
    const std::string padDone = newLabel(ctx, "rpad.done.");
    sealBlock(ctx, branchTo(padLoop));
    beginBlock(ctx, padLoop);
    {
        const irs::ValueId L = emitLoad(ctx, len, irs::IrType::I32);
        const irs::ValueId lt =
            emitBinaryOp(ctx, irs::Op::CmpLt, L, emitConstI32(ctx, 24), irs::IrType::Bool);
        const std::string body = newLabel(ctx, "rpad.body.");
        sealBlock(ctx, branchIfTo(lt, body, padDone));
        beginBlock(ctx, body);
        const irs::ValueId cur = emitLoad(ctx, buf, irs::IrType::StringRef);
        const irs::ValueId space = emitConstString(ctx, " ");
        const irs::ValueId next =
            emitRuntimeCall(ctx, "str.concat", irs::IrType::StringRef, {cur, space});
        emitStore(ctx, buf, next);
        emitStore(ctx, len, emitBinaryOp(ctx, irs::Op::Add, L, emitConstI32(ctx, 1), irs::IrType::I32));
        sealBlock(ctx, branchTo(padLoop));
    }
    beginBlock(ctx, padDone);
    // If longer than 24, still write full STR (size not strictly enforced for oversize).
    const irs::ValueId data = emitLoad(ctx, buf, irs::IrType::StringRef);
    (void)emitRuntimeCall(ctx, "hostfs.write", irs::IrType::I32, {handle, data});
}

irs::ValueId lowerReadRealAscii(LowerCtx &ctx, std::string_view fileName, irs::ValueId handle) {
    const irs::ValueId n = emitConstI32(ctx, 24);
    const irs::ValueId chunk =
        emitRuntimeCall(ctx, "hostfs.read", irs::IrType::StringRef, {handle, n});
    const irs::ValueId empty = emitConstString(ctx, "");
    const irs::ValueId gotEmpty =
        emitBinaryOp(ctx, irs::Op::CmpEq, chunk, empty, irs::IrType::Bool);
    const irs::Operand dest = declareTempLocal(ctx, "$rr.", irs::IrType::F64);
    const std::string atEof = newLabel(ctx, "rr.eof.");
    const std::string have = newLabel(ctx, "rr.ok.");
    const std::string merge = newLabel(ctx, "rr.m.");
    sealBlock(ctx, branchIfTo(gotEmpty, atEof, have));
    beginBlock(ctx, atEof);
    storeFileSlot(ctx, fileName, "eof", emitConstI32(ctx, 1));
    emitStore(ctx, dest, emitConstF64(ctx, 0.0));
    sealBlock(ctx, branchTo(merge));
    beginBlock(ctx, have);
    emitStore(ctx, dest,
              emitRuntimeCall(ctx, "coerce.flt", irs::IrType::F64, {chunk}));
    sealBlock(ctx, branchTo(merge));
    beginBlock(ctx, merge);
    return emitLoad(ctx, dest, irs::IrType::F64);
}

void lowerWriteScalarComponent(LowerCtx &ctx, std::string_view fileName, const TypePtr &elemType,
                               irs::ValueId value) {
    const irs::ValueId handle = loadFileSlot(ctx, fileName, "h", irs::IrType::I32);
    switch (canonicalTag(elemType)) {
    case TypeTag::Integer:
    case TypeTag::Boolean:
        lowerWriteI32LE(ctx, handle, value);
        break;
    case TypeTag::Char:
        lowerWriteCharByte(ctx, handle, value);
        break;
    case TypeTag::Real:
        lowerWriteRealAscii(ctx, handle, value);
        break;
    default:
        break;
    }
}

irs::ValueId lowerReadScalarComponent(LowerCtx &ctx, std::string_view fileName,
                                      const TypePtr &elemType) {
    const irs::ValueId handle = loadFileSlot(ctx, fileName, "h", irs::IrType::I32);
    switch (canonicalTag(elemType)) {
    case TypeTag::Integer:
    case TypeTag::Boolean:
        return lowerReadI32LE(ctx, fileName, handle);
    case TypeTag::Char:
        return lowerReadCharByte(ctx, fileName, handle);
    case TypeTag::Real:
        return lowerReadRealAscii(ctx, fileName, handle);
    default:
        return emitConstI32(ctx, 0);
    }
}

void lowerWriteRecordComponent(LowerCtx &ctx, std::string_view fileName,
                               std::string_view recordVar, const TypePtr &recordType) {
    const TypePtr peeled = peelTypeAliases(recordType);
    if (!peeled || peeled->tag != TypeTag::Record) {
        return;
    }
    for (const RecordField &field : peeled->fields) {
        const auto slot = resolveFieldSlot(ctx, recordVar, field.name);
        if (!slot) {
            continue;
        }
        const irs::ValueId v = emitLoad(ctx, slot->operand, slot->type);
        lowerWriteScalarComponent(ctx, fileName, field.type, v);
    }
}

void lowerReadRecordComponent(LowerCtx &ctx, std::string_view fileName,
                              std::string_view recordVar, const TypePtr &recordType) {
    const TypePtr peeled = peelTypeAliases(recordType);
    if (!peeled || peeled->tag != TypeTag::Record) {
        return;
    }
    for (const RecordField &field : peeled->fields) {
        const auto slot = resolveFieldSlot(ctx, recordVar, field.name);
        if (!slot) {
            continue;
        }
        const irs::ValueId v = lowerReadScalarComponent(ctx, fileName, field.type);
        emitStore(ctx, slot->operand, v);
    }
}

void lowerTypedFileWrite(LowerCtx &ctx, const ast::Stmt &stmt) {
    if (stmt.args.size() < 2 || !stmt.args[0] || !stmt.args[1]) {
        return;
    }
    const std::string fileName = stmt.args[0]->text;
    TypePtr ft = fileVarType(ctx, foldAsciiLower(fileName));
    while (ft && ft->tag == TypeTag::Alias) {
        ft = ft->canonical;
    }
    if (!ft || ft->tag != TypeTag::File || !ft->element) {
        return;
    }
    const TypeTag et = canonicalTag(ft->element);
    if (et == TypeTag::Record) {
        if (stmt.args[1]->kind != ast::ExprKind::Identifier) {
            return;
        }
        lowerWriteRecordComponent(ctx, fileName, stmt.args[1]->text, ft->element);
        return;
    }
    const irs::ValueId v = lowerExpr(ctx, *stmt.args[1]);
    lowerWriteScalarComponent(ctx, fileName, ft->element, v);
}

void lowerTypedFileRead(LowerCtx &ctx, const ast::Stmt &stmt) {
    if (stmt.args.size() < 2 || !stmt.args[0] || !stmt.args[1]) {
        return;
    }
    const std::string fileName = stmt.args[0]->text;
    TypePtr ft = fileVarType(ctx, foldAsciiLower(fileName));
    while (ft && ft->tag == TypeTag::Alias) {
        ft = ft->canonical;
    }
    if (!ft || ft->tag != TypeTag::File || !ft->element) {
        return;
    }
    const TypeTag et = canonicalTag(ft->element);
    if (et == TypeTag::Record) {
        if (stmt.args[1]->kind != ast::ExprKind::Identifier) {
            return;
        }
        lowerReadRecordComponent(ctx, fileName, stmt.args[1]->text, ft->element);
        return;
    }
    if (stmt.args[1]->kind != ast::ExprKind::Identifier) {
        return;
    }
    const auto slot = resolveSlot(ctx, foldAsciiLower(stmt.args[1]->text));
    if (!slot) {
        return;
    }
    const irs::ValueId v = lowerReadScalarComponent(ctx, fileName, ft->element);
    emitStore(ctx, slot->operand, v);
}

void lowerTextFileWrite(LowerCtx &ctx, const ast::Stmt &stmt, bool newline) {
    if (stmt.args.empty() || !stmt.args[0]) {
        return;
    }
    const std::string fileName = stmt.args[0]->text;
    const irs::ValueId handle = loadFileSlot(ctx, fileName, "h", irs::IrType::I32);
    irs::ValueId data = emitConstString(ctx, "");
    for (std::size_t i = 1; i < stmt.args.size(); ++i) {
        if (!stmt.args[i]) {
            continue;
        }
        const irs::ValueId piece = lowerToString(ctx, *stmt.args[i]);
        data = emitRuntimeCall(ctx, "str.concat", irs::IrType::StringRef, {data, piece});
    }
    if (newline) {
        data = emitRuntimeCall(ctx, "str.concat", irs::IrType::StringRef,
                               {data, emitConstString(ctx, "\n")});
    }
    (void)emitRuntimeCall(ctx, "hostfs.write", irs::IrType::I32, {handle, data});
}

irs::ValueId lowerTextConsumePeek(LowerCtx &ctx, std::string_view fileName) {
    lowerTextFillPeek(ctx, fileName);
    const irs::ValueId peek = loadFileSlot(ctx, fileName, "peek", irs::IrType::StringRef);
    storeFileSlot(ctx, fileName, "peek", emitConstString(ctx, ""));
    return peek;
}

void lowerTextReadCharVar(LowerCtx &ctx, std::string_view fileName, const ResolvedSlot &slot) {
    const irs::ValueId peek = lowerTextConsumePeek(ctx, fileName);
    const irs::ValueId code = emitRuntimeCall(ctx, "str.seq", irs::IrType::I32, {peek});
    emitStore(ctx, slot.operand, code);
}

void lowerTextReadStringVar(LowerCtx &ctx, std::string_view fileName, const ResolvedSlot &slot,
                            bool readln) {
    const irs::Operand dest = declareTempLocal(ctx, "$trs.", irs::IrType::StringRef);
    emitStore(ctx, dest, emitConstString(ctx, ""));
    const std::string loop = newLabel(ctx, "tread.");
    const std::string done = newLabel(ctx, "tread.done.");
    sealBlock(ctx, branchTo(loop));
    beginBlock(ctx, loop);
    lowerTextFillPeek(ctx, fileName);
    {
        const irs::ValueId eofFlag = loadFileSlot(ctx, fileName, "eof", irs::IrType::I32);
        const irs::ValueId atEof =
            emitBinaryOp(ctx, irs::Op::CmpNe, eofFlag, emitConstI32(ctx, 0), irs::IrType::Bool);
        const std::string body = newLabel(ctx, "tread.body.");
        sealBlock(ctx, branchIfTo(atEof, done, body));
        beginBlock(ctx, body);
        const irs::ValueId eolnFlag = loadFileSlot(ctx, fileName, "eoln", irs::IrType::I32);
        const irs::ValueId atEoln =
            emitBinaryOp(ctx, irs::Op::CmpNe, eolnFlag, emitConstI32(ctx, 0), irs::IrType::Bool);
        const std::string take = newLabel(ctx, "tread.take.");
        // Stop before newline; `readln`'s trailing consume (in lowerTextFileRead) eats EOL.
        sealBlock(ctx, branchIfTo(atEoln, done, take));
        beginBlock(ctx, take);
        {
            const irs::ValueId ch = lowerTextConsumePeek(ctx, fileName);
            const irs::ValueId cur = emitLoad(ctx, dest, irs::IrType::StringRef);
            emitStore(ctx, dest,
                      emitRuntimeCall(ctx, "str.concat", irs::IrType::StringRef, {cur, ch}));
            if (!readln) {
                // Token read: stop at whitespace.
                lowerTextFillPeek(ctx, fileName);
                const irs::ValueId peek =
                    loadFileSlot(ctx, fileName, "peek", irs::IrType::StringRef);
                const irs::ValueId code =
                    emitRuntimeCall(ctx, "str.seq", irs::IrType::I32, {peek});
                const irs::ValueId sp = emitConstI32(ctx, static_cast<unsigned char>(' '));
                const irs::ValueId isSp =
                    emitBinaryOp(ctx, irs::Op::CmpEq, code, sp, irs::IrType::Bool);
                const irs::ValueId isEof =
                    emitBinaryOp(ctx, irs::Op::CmpNe,
                                 loadFileSlot(ctx, fileName, "eof", irs::IrType::I32),
                                 emitConstI32(ctx, 0), irs::IrType::Bool);
                const irs::ValueId isEoln =
                    emitBinaryOp(ctx, irs::Op::CmpNe,
                                 loadFileSlot(ctx, fileName, "eoln", irs::IrType::I32),
                                 emitConstI32(ctx, 0), irs::IrType::Bool);
                const irs::ValueId stop1 =
                    emitBinaryOp(ctx, irs::Op::Or, isSp, isEof, irs::IrType::Bool);
                const irs::ValueId stop =
                    emitBinaryOp(ctx, irs::Op::Or, stop1, isEoln, irs::IrType::Bool);
                const std::string cont = newLabel(ctx, "tread.cont.");
                sealBlock(ctx, branchIfTo(stop, done, cont));
                beginBlock(ctx, cont);
            }
            sealBlock(ctx, branchTo(loop));
        }
    }
    beginBlock(ctx, done);
    emitStore(ctx, slot.operand, emitLoad(ctx, dest, irs::IrType::StringRef));
}

void lowerTextReadIntVar(LowerCtx &ctx, std::string_view fileName, const ResolvedSlot &slot,
                         bool asReal) {
    // Skip spaces/newlines, accumulate token, coerce.
    const std::string skip = newLabel(ctx, "ti.skip.");
    const std::string acc = newLabel(ctx, "ti.acc.");
    const std::string done = newLabel(ctx, "ti.done.");
    const irs::Operand tok = declareTempLocal(ctx, "$ti.", irs::IrType::StringRef);
    emitStore(ctx, tok, emitConstString(ctx, ""));
    sealBlock(ctx, branchTo(skip));
    beginBlock(ctx, skip);
    lowerTextFillPeek(ctx, fileName);
    {
        const irs::ValueId eofFlag = loadFileSlot(ctx, fileName, "eof", irs::IrType::I32);
        const irs::ValueId atEof =
            emitBinaryOp(ctx, irs::Op::CmpNe, eofFlag, emitConstI32(ctx, 0), irs::IrType::Bool);
        const std::string check = newLabel(ctx, "ti.chk.");
        sealBlock(ctx, branchIfTo(atEof, done, check));
        beginBlock(ctx, check);
        const irs::ValueId peek = loadFileSlot(ctx, fileName, "peek", irs::IrType::StringRef);
        const irs::ValueId code = emitRuntimeCall(ctx, "str.seq", irs::IrType::I32, {peek});
        const irs::ValueId sp = emitConstI32(ctx, static_cast<unsigned char>(' '));
        const irs::ValueId nl = emitConstI32(ctx, static_cast<unsigned char>('\n'));
        const irs::ValueId isSp =
            emitBinaryOp(ctx, irs::Op::CmpEq, code, sp, irs::IrType::Bool);
        const irs::ValueId isNl =
            emitBinaryOp(ctx, irs::Op::CmpEq, code, nl, irs::IrType::Bool);
        const irs::ValueId isWs =
            emitBinaryOp(ctx, irs::Op::Or, isSp, isNl, irs::IrType::Bool);
        const std::string eat = newLabel(ctx, "ti.eat.");
        sealBlock(ctx, branchIfTo(isWs, eat, acc));
        beginBlock(ctx, eat);
        (void)lowerTextConsumePeek(ctx, fileName);
        sealBlock(ctx, branchTo(skip));
    }
    beginBlock(ctx, acc);
    lowerTextFillPeek(ctx, fileName);
    {
        const irs::ValueId eofFlag = loadFileSlot(ctx, fileName, "eof", irs::IrType::I32);
        const irs::ValueId atEof =
            emitBinaryOp(ctx, irs::Op::CmpNe, eofFlag, emitConstI32(ctx, 0), irs::IrType::Bool);
        const std::string take = newLabel(ctx, "ti.take.");
        sealBlock(ctx, branchIfTo(atEof, done, take));
        beginBlock(ctx, take);
        const irs::ValueId peek = loadFileSlot(ctx, fileName, "peek", irs::IrType::StringRef);
        const irs::ValueId code = emitRuntimeCall(ctx, "str.seq", irs::IrType::I32, {peek});
        const irs::ValueId sp = emitConstI32(ctx, static_cast<unsigned char>(' '));
        const irs::ValueId nl = emitConstI32(ctx, static_cast<unsigned char>('\n'));
        const irs::ValueId isSp =
            emitBinaryOp(ctx, irs::Op::CmpEq, code, sp, irs::IrType::Bool);
        const irs::ValueId isNl =
            emitBinaryOp(ctx, irs::Op::CmpEq, code, nl, irs::IrType::Bool);
        const irs::ValueId isWs =
            emitBinaryOp(ctx, irs::Op::Or, isSp, isNl, irs::IrType::Bool);
        const std::string stop = newLabel(ctx, "ti.stop.");
        const std::string more = newLabel(ctx, "ti.more.");
        sealBlock(ctx, branchIfTo(isWs, stop, more));
        beginBlock(ctx, stop);
        sealBlock(ctx, branchTo(done));
        beginBlock(ctx, more);
        const irs::ValueId ch = lowerTextConsumePeek(ctx, fileName);
        const irs::ValueId cur = emitLoad(ctx, tok, irs::IrType::StringRef);
        emitStore(ctx, tok,
                  emitRuntimeCall(ctx, "str.concat", irs::IrType::StringRef, {cur, ch}));
        sealBlock(ctx, branchTo(acc));
    }
    beginBlock(ctx, done);
    const irs::ValueId text = emitLoad(ctx, tok, irs::IrType::StringRef);
    if (asReal) {
        emitStore(ctx, slot.operand,
                  emitRuntimeCall(ctx, "coerce.flt", irs::IrType::F64, {text}));
    } else {
        emitStore(ctx, slot.operand,
                  emitRuntimeCall(ctx, "coerce.int", irs::IrType::I32, {text}));
    }
}

void lowerTextFileRead(LowerCtx &ctx, const ast::Stmt &stmt, bool readln) {
    if (stmt.args.empty() || !stmt.args[0]) {
        return;
    }
    const std::string fileName = stmt.args[0]->text;
    for (std::size_t i = 1; i < stmt.args.size(); ++i) {
        if (!stmt.args[i] || stmt.args[i]->kind != ast::ExprKind::Identifier) {
            continue;
        }
        const auto slot = resolveSlot(ctx, foldAsciiLower(stmt.args[i]->text));
        if (!slot) {
            continue;
        }
        switch (slot->type) {
        case irs::IrType::Char:
            lowerTextReadCharVar(ctx, fileName, *slot);
            break;
        case irs::IrType::StringRef:
            lowerTextReadStringVar(ctx, fileName, *slot, readln);
            break;
        case irs::IrType::I32:
        case irs::IrType::Bool:
            lowerTextReadIntVar(ctx, fileName, *slot, false);
            break;
        case irs::IrType::F64:
            lowerTextReadIntVar(ctx, fileName, *slot, true);
            break;
        default:
            break;
        }
    }
    if (readln) {
        // Consume through end of line if not already.
        const std::string loop = newLabel(ctx, "rln.");
        const std::string done = newLabel(ctx, "rln.done.");
        sealBlock(ctx, branchTo(loop));
        beginBlock(ctx, loop);
        lowerTextFillPeek(ctx, fileName);
        const irs::ValueId eofFlag = loadFileSlot(ctx, fileName, "eof", irs::IrType::I32);
        const irs::ValueId atEof =
            emitBinaryOp(ctx, irs::Op::CmpNe, eofFlag, emitConstI32(ctx, 0), irs::IrType::Bool);
        const std::string body = newLabel(ctx, "rln.body.");
        sealBlock(ctx, branchIfTo(atEof, done, body));
        beginBlock(ctx, body);
        const irs::ValueId eolnFlag = loadFileSlot(ctx, fileName, "eoln", irs::IrType::I32);
        const irs::ValueId atEoln =
            emitBinaryOp(ctx, irs::Op::CmpNe, eolnFlag, emitConstI32(ctx, 0), irs::IrType::Bool);
        const std::string eat = newLabel(ctx, "rln.eat.");
        const std::string cont = newLabel(ctx, "rln.cont.");
        sealBlock(ctx, branchIfTo(atEoln, eat, cont));
        beginBlock(ctx, eat);
        (void)lowerTextConsumePeek(ctx, fileName);
        sealBlock(ctx, branchTo(done));
        beginBlock(ctx, cont);
        (void)lowerTextConsumePeek(ctx, fileName);
        sealBlock(ctx, branchTo(loop));
        beginBlock(ctx, done);
    }
}

void lowerWriteCall(LowerCtx &ctx, const std::string &foldedName,
                    const std::vector<std::unique_ptr<ast::Expr>> &args) {
    std::vector<irs::ValueId> argValues;
    std::vector<irs::IrType> argTypes;
    argValues.reserve(args.size());
    argTypes.reserve(args.size());
    for (const auto &arg : args) {
        if (arg) {
            argValues.push_back(lowerExpr(ctx, *arg));
            argTypes.push_back(toIrType(arg->type));
        }
    }
    irs::Instr call;
    call.op = irs::Op::CallRuntime;
    call.type = irs::IrType::Void;
    call.text = foldedName;
    call.args = std::move(argValues);
    call.argTypes = std::move(argTypes);
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
    if (folded == "assign") {
        lowerAssignFile(ctx, stmt);
        return;
    }
    if (folded == "reset") {
        lowerResetRewrite(ctx, stmt, true);
        return;
    }
    if (folded == "rewrite") {
        lowerResetRewrite(ctx, stmt, false);
        return;
    }
    if (folded == "close") {
        if (!stmt.args.empty() && stmt.args[0]) {
            lowerCloseFile(ctx, stmt.args[0]->text);
        }
        return;
    }
    if (folded == "write" || folded == "writeln") {
        if (!stmt.args.empty() && stmt.args[0] &&
            stmt.args[0]->kind == ast::ExprKind::Identifier &&
            isFileVarName(ctx, foldAsciiLower(stmt.args[0]->text))) {
            const TypeTag ft = canonicalTag(stmt.args[0]->type);
            if (ft == TypeTag::File) {
                lowerTypedFileWrite(ctx, stmt);
                return;
            }
            if (ft == TypeTag::Text) {
                lowerTextFileWrite(ctx, stmt, folded == "writeln");
                return;
            }
        }
        lowerWriteCall(ctx, folded, stmt.args);
        return;
    }
    if (folded == "read" || folded == "readln") {
        if (!stmt.args.empty() && stmt.args[0] &&
            stmt.args[0]->kind == ast::ExprKind::Identifier &&
            isFileVarName(ctx, foldAsciiLower(stmt.args[0]->text))) {
            const TypeTag ft = canonicalTag(stmt.args[0]->type);
            if (ft == TypeTag::File) {
                lowerTypedFileRead(ctx, stmt);
                return;
            }
            if (ft == TypeTag::Text) {
                lowerTextFileRead(ctx, stmt, folded == "readln");
                return;
            }
        }
        lowerReadCall(ctx, folded, stmt.args);
        return;
    }

    std::vector<irs::ArrayCopySetup> matCopies;
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
        if (destSlot->type == irs::IrType::ArrayRef && stmt.value) {
            const auto srcSlot = resolveArrayBaseSlot(ctx, *stmt.value);
            if (!srcSlot || srcSlot->type != irs::IrType::ArrayRef) {
                ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                                       "IR lowering: array copy requires an array variable source");
                return;
            }
            emitArrayCopy(ctx, destSlot->operand, srcSlot->operand);
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

irs::ValueId emitCaseLabelMatch(LowerCtx &ctx, irs::ValueId selector, const ast::CaseLabel &label) {
    const irs::ValueId lo = label.lo ? lowerExpr(ctx, *label.lo) : emitConstI32(ctx, 0);
    if (!label.hi) {
        return emitBinaryOp(ctx, irs::Op::CmpEq, selector, lo, irs::IrType::Bool);
    }
    const irs::ValueId hi = lowerExpr(ctx, *label.hi);
    const irs::ValueId ge = emitBinaryOp(ctx, irs::Op::CmpGe, selector, lo, irs::IrType::Bool);
    const irs::ValueId le = emitBinaryOp(ctx, irs::Op::CmpLe, selector, hi, irs::IrType::Bool);
    return emitBinaryOp(ctx, irs::Op::And, ge, le, irs::IrType::Bool);
}

irs::ValueId emitCaseArmMatch(LowerCtx &ctx, irs::ValueId selector, const ast::CaseArm &arm) {
    if (arm.labels.empty()) {
        return emitConstBool(ctx, false);
    }
    irs::ValueId match = emitCaseLabelMatch(ctx, selector, arm.labels.front());
    for (std::size_t i = 1; i < arm.labels.size(); ++i) {
        const irs::ValueId next = emitCaseLabelMatch(ctx, selector, arm.labels[i]);
        match = emitBinaryOp(ctx, irs::Op::Or, match, next, irs::IrType::Bool);
    }
    return match;
}

void lowerCase(LowerCtx &ctx, const ast::Stmt &stmt) {
    const irs::ValueId selector =
        stmt.condition ? lowerExpr(ctx, *stmt.condition) : emitConstI32(ctx, 0);
    const std::string mergeLabel = newLabel(ctx, "case.merge.");
    const bool hasElse = static_cast<bool>(stmt.elseBranch);
    const std::string elseLabel = hasElse ? newLabel(ctx, "case.else.") : std::string();

    std::vector<std::string> testLabels;
    std::vector<std::string> armLabels;
    testLabels.reserve(stmt.caseArms.size());
    armLabels.reserve(stmt.caseArms.size());
    for (std::size_t i = 0; i < stmt.caseArms.size(); ++i) {
        testLabels.push_back(newLabel(ctx, "case.test."));
        armLabels.push_back(newLabel(ctx, "case.arm."));
    }

    if (stmt.caseArms.empty()) {
        if (hasElse) {
            sealBlock(ctx, branchTo(elseLabel));
            beginBlock(ctx, elseLabel);
            lowerStmt(ctx, *stmt.elseBranch);
            sealBlock(ctx, branchTo(mergeLabel));
            beginBlock(ctx, mergeLabel);
        }
        return;
    }

    sealBlock(ctx, branchTo(testLabels.front()));

    for (std::size_t i = 0; i < stmt.caseArms.size(); ++i) {
        beginBlock(ctx, testLabels[i]);
        const irs::ValueId match = emitCaseArmMatch(ctx, selector, stmt.caseArms[i]);
        std::string missTarget;
        if (i + 1 < stmt.caseArms.size()) {
            missTarget = testLabels[i + 1];
        } else if (hasElse) {
            missTarget = elseLabel;
        } else {
            missTarget = mergeLabel;
        }
        sealBlock(ctx, branchIfTo(match, armLabels[i], missTarget));
    }

    for (std::size_t i = 0; i < stmt.caseArms.size(); ++i) {
        beginBlock(ctx, armLabels[i]);
        if (stmt.caseArms[i].body) {
            lowerStmt(ctx, *stmt.caseArms[i].body);
        }
        sealBlock(ctx, branchTo(mergeLabel));
    }

    if (hasElse) {
        beginBlock(ctx, elseLabel);
        lowerStmt(ctx, *stmt.elseBranch);
        sealBlock(ctx, branchTo(mergeLabel));
    }

    beginBlock(ctx, mergeLabel);
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
    case ast::StmtKind::Case:
        lowerCase(ctx, stmt);
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
            if (peeled && (peeled->tag == TypeTag::Text || peeled->tag == TypeTag::File)) {
                declareFileSlots(ctx, name, localType);
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
    // Array value parameters are dimmed at the call site before MAT_COPY; only locals
    // are dimensioned in the callee entry block.
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
    for (const auto &[folded, fileType] : ctx.fileVars) {
        (void)fileType;
        // Locals are stored under original casing in `locals[].name`; resolve via slots.
        storeFileSlot(ctx, folded, "path", emitConstString(ctx, ""));
        storeFileSlot(ctx, folded, "h", emitConstI32(ctx, 0));
        storeFileSlot(ctx, folded, "rd", emitConstI32(ctx, 0));
        storeFileSlot(ctx, folded, "eof", emitConstI32(ctx, 0));
        if (canonicalTag(fileType) == TypeTag::Text) {
            storeFileSlot(ctx, folded, "peek", emitConstString(ctx, ""));
            storeFileSlot(ctx, folded, "eoln", emitConstI32(ctx, 0));
        }
    }

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
