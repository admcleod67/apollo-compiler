#include "apollo/pascal/ir/Lower.hpp"

#include "apollo/common/Diagnostic.hpp"

#include <cstdint>
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
    case TypeTag::Alias: // canonicalTag() already peels aliases
    case TypeTag::Error:
        return irs::IrType::Error;
    }
    return irs::IrType::Error;
}

struct LowerCtx {
    const SymbolTable &symbols;
    apollo::common::DiagnosticEngine &diagnostics;
    irs::Function &function;
    irs::BasicBlock &block;
    /// Folded var/param name -> `Function::locals` index.
    std::unordered_map<std::string, std::uint32_t> localSlots;
    /// Folded const name -> its literal declaration expr (for inlining at each use).
    std::unordered_map<std::string, const ast::Expr *> constExprs;
};

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

irs::ValueId lowerExpr(LowerCtx &ctx, const ast::Expr &expr);
void lowerStmt(LowerCtx &ctx, const ast::Stmt &stmt);

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

irs::ValueId emitLoadLocal(LowerCtx &ctx, std::uint32_t slot, irs::IrType type) {
    irs::Instr instr;
    instr.op = irs::Op::LoadLocal;
    instr.type = type;
    instr.result = ctx.function.newTemp();
    instr.a = makeLocalOperand(slot);
    const irs::ValueId result = instr.result;
    ctx.block.body.push_back(std::move(instr));
    return result;
}

void emitStoreLocal(LowerCtx &ctx, std::uint32_t slot, irs::ValueId value) {
    irs::Instr instr;
    instr.op = irs::Op::StoreLocal;
    instr.type = irs::IrType::Void;
    instr.result = ctx.function.newTemp();
    instr.a = makeLocalOperand(slot);
    instr.b = makeValueOperand(value);
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

irs::ValueId lowerUserCall(LowerCtx &ctx, const std::string &calleeName,
                           std::vector<irs::ValueId> args, irs::IrType resultType) {
    irs::Instr call;
    call.op = irs::Op::Call;
    call.type = resultType;
    call.result = ctx.function.newTemp();
    call.text = calleeName;
    call.args = std::move(args);
    const irs::ValueId result = call.result;
    ctx.block.body.push_back(std::move(call));
    return result;
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
    const Symbol *symbol = ctx.symbols.lookup(expr.text);
    if (!symbol) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                               "IR lowering: undeclared identifier '" + expr.text + "'");
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
    case SymbolKind::Var:
    case SymbolKind::Param: {
        const auto slot = ctx.localSlots.find(folded);
        if (slot == ctx.localSlots.end()) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, expr.range.begin,
                                   "IR lowering: '" + expr.text + "' has no IR local slot");
            return emitConstI32(ctx, 0);
        }
        return emitLoadLocal(ctx, slot->second, toIrType(symbol->type));
    }
    case SymbolKind::Function:
        // Bare function identifier is a zero-argument call; M3 already checked arity.
        return lowerUserCall(ctx, expr.text, {}, toIrType(symbol->type));
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
    const irs::ValueId left = expr.left ? lowerExpr(ctx, *expr.left) : emitConstI32(ctx, 0);
    const irs::ValueId right = expr.right ? lowerExpr(ctx, *expr.right) : emitConstI32(ctx, 0);
    return emitBinaryOp(ctx, toBinaryOp(expr.binaryOp), left, right, toIrType(expr.type));
}

irs::ValueId lowerCallExpr(LowerCtx &ctx, const ast::Expr &expr) {
    // Builtins are statement-only per M3 (never valid as an expression), so a Call
    // Expr here is always a user function call.
    std::vector<irs::ValueId> argValues;
    argValues.reserve(expr.args.size());
    for (const auto &arg : expr.args) {
        if (arg) {
            argValues.push_back(lowerExpr(ctx, *arg));
        }
    }
    return lowerUserCall(ctx, expr.text, std::move(argValues), toIrType(expr.type));
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
    call.result = ctx.function.newTemp();
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
        const auto slot = ctx.localSlots.find(argFolded);
        if (slot == ctx.localSlots.end()) {
            ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, arg->range.begin,
                                   "IR lowering: '" + arg->text + "' has no IR local slot for '" +
                                       foldedName + "'");
            continue;
        }
        const irs::IrType varType = ctx.function.locals[slot->second].type;

        irs::Instr call;
        call.op = irs::Op::CallRuntime;
        call.type = varType;
        call.result = ctx.function.newTemp();
        call.text = foldedName;
        const irs::ValueId readValue = call.result;
        ctx.block.body.push_back(std::move(call));

        emitStoreLocal(ctx, slot->second, readValue);
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

    std::vector<irs::ValueId> argValues;
    argValues.reserve(stmt.args.size());
    for (const auto &arg : stmt.args) {
        if (arg) {
            argValues.push_back(lowerExpr(ctx, *arg));
        }
    }
    // Result (if any) is discarded in statement context; Stage 3 attaches real callee
    // return types once subprogram bodies are lowered.
    (void)lowerUserCall(ctx, stmt.name, std::move(argValues), irs::IrType::Void);
}

void lowerAssign(LowerCtx &ctx, const ast::Stmt &stmt) {
    const irs::ValueId value = stmt.value ? lowerExpr(ctx, *stmt.value) : emitConstI32(ctx, 0);
    const std::string folded = foldAsciiLower(stmt.name);
    const auto slot = ctx.localSlots.find(folded);
    if (slot == ctx.localSlots.end()) {
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "IR lowering: assignment target '" + stmt.name +
                                   "' has no IR local slot");
        return;
    }
    emitStoreLocal(ctx, slot->second, value);
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
    case ast::StmtKind::While:
    case ast::StmtKind::Repeat:
    case ast::StmtKind::For:
        ctx.diagnostics.report(apollo::common::DiagnosticSeverity::Error, stmt.range.begin,
                               "control flow not lowered until Milestone 4 Stage 3");
        break;
    }
}

} // namespace

apollo::ir::Module lowerToIr(const ast::Program &program, const SymbolTable &symbols,
                             apollo::common::DiagnosticEngine &diagnostics) {
    irs::Module module;
    module.name = program.name;

    irs::Function function;
    function.name = "main";
    function.returnType = irs::IrType::Void;

    irs::BasicBlock entry;
    entry.label = "entry";

    LowerCtx ctx{symbols, diagnostics, function, entry, {}, {}};

    for (const auto &decl : program.block.consts) {
        ctx.constExprs.emplace(foldAsciiLower(decl.name), decl.value.get());
    }

    for (const auto &decl : program.block.vars) {
        for (const auto &name : decl.names) {
            const Symbol *symbol = symbols.lookup(name);
            const irs::IrType localType = symbol ? toIrType(symbol->type) : irs::IrType::Error;
            const auto slot = static_cast<std::uint32_t>(function.locals.size());
            function.locals.push_back(irs::Local{name, localType});
            ctx.localSlots.emplace(foldAsciiLower(name), slot);
        }
    }

    // User subprograms are not lowered until Stage 3; call sites still lower fine.
    for (const auto &stmt : program.block.body.statements) {
        lowerStmt(ctx, stmt);
    }

    entry.term.kind = irs::TerminatorKind::Return;
    function.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(function));
    return module;
}

} // namespace apollo::pascal::ir
