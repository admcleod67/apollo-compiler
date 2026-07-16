#include "apollo/codegen/Emit.hpp"
#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/ir/Ir.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "ir_emit_test: " << message << '\n';
    return 1;
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    // Hand-built IR mirroring hello: const.string + call.runtime @writeln + return.
    {
        apollo::common::SourceFile source =
            apollo::common::SourceFile::fromString("hello_ir.pas", "program Hello; begin end.");
        apollo::common::DiagnosticEngine diagnostics(source);

        apollo::ir::Module module;
        module.name = "Hello";

        apollo::ir::Function mainFn;
        mainFn.name = "main";
        mainFn.returnType = apollo::ir::IrType::Void;

        apollo::ir::BasicBlock entry;
        entry.label = "entry";

        apollo::ir::Instr msg;
        msg.op = apollo::ir::Op::ConstString;
        msg.type = apollo::ir::IrType::StringRef;
        msg.result = mainFn.newTemp();
        msg.text = "Hello, Gemini!";
        entry.body.push_back(msg);

        apollo::ir::Instr call;
        call.op = apollo::ir::Op::CallRuntime;
        call.type = apollo::ir::IrType::Void;
        call.result = mainFn.newTemp();
        call.text = "writeln";
        call.args.push_back(msg.result);
        entry.body.push_back(call);

        apollo::ir::Terminator ret;
        ret.kind = apollo::ir::TerminatorKind::Return;
        entry.term = ret;

        mainFn.blocks.push_back(std::move(entry));
        module.functions.push_back(std::move(mainFn));

        const std::string tbc = apollo::codegen::emitTbc(module, diagnostics);
        if (diagnostics.errorCount() != 0 || tbc.empty()) {
            return fail("hello-shaped IR should emit with zero diagnostics");
        }
        if (!contains(tbc, "main:") || !contains(tbc, "PUSH_STR \"Hello, Gemini!\"") ||
            !contains(tbc, "PRINT_VAL") || !contains(tbc, "PRINT_EOL") ||
            !contains(tbc, "HALT")) {
            return fail("hello-shaped .tbc missing expected opcodes");
        }
    }

    // Multi-block CFG with BranchIf should emit JUMP / JZ.
    {
        apollo::common::SourceFile source =
            apollo::common::SourceFile::fromString("cfg.pas", "program P; begin end.");
        apollo::common::DiagnosticEngine diagnostics(source);

        apollo::ir::Module module;
        module.name = "P";
        apollo::ir::Function mainFn;
        mainFn.name = "main";
        mainFn.returnType = apollo::ir::IrType::Void;

        apollo::ir::BasicBlock entry;
        entry.label = "entry";
        apollo::ir::Instr one;
        one.op = apollo::ir::Op::ConstI32;
        one.type = apollo::ir::IrType::I32;
        one.result = mainFn.newTemp();
        one.i64 = 1;
        entry.body.push_back(one);
        entry.term.kind = apollo::ir::TerminatorKind::BranchIf;
        entry.term.value = one.result;
        entry.term.target = "then";
        entry.term.falseTarget = "end";

        apollo::ir::BasicBlock thenBlock;
        thenBlock.label = "then";
        thenBlock.term.kind = apollo::ir::TerminatorKind::Branch;
        thenBlock.term.target = "end";

        apollo::ir::BasicBlock endBlock;
        endBlock.label = "end";
        endBlock.term.kind = apollo::ir::TerminatorKind::Return;

        mainFn.blocks.push_back(std::move(entry));
        mainFn.blocks.push_back(std::move(thenBlock));
        mainFn.blocks.push_back(std::move(endBlock));
        module.functions.push_back(std::move(mainFn));

        const std::string tbc = apollo::codegen::emitTbc(module, diagnostics);
        if (diagnostics.errorCount() != 0 || tbc.empty()) {
            return fail("multi-block BranchIf IR should emit with zero diagnostics");
        }
        if (!contains(tbc, "JZ") || !contains(tbc, "JUMP") || !contains(tbc, "main$then") ||
            !contains(tbc, "HALT")) {
            return fail("multi-block .tbc missing JZ / JUMP / labels / HALT");
        }
    }

    // User Call between main and a stub procedure.
    {
        apollo::common::SourceFile source =
            apollo::common::SourceFile::fromString("call.pas", "program P; begin end.");
        apollo::common::DiagnosticEngine diagnostics(source);

        apollo::ir::Module module;
        module.name = "P";

        apollo::ir::Function mainFn;
        mainFn.name = "main";
        mainFn.returnType = apollo::ir::IrType::Void;
        apollo::ir::BasicBlock mainEntry;
        mainEntry.label = "entry";
        apollo::ir::Instr arg;
        arg.op = apollo::ir::Op::ConstI32;
        arg.type = apollo::ir::IrType::I32;
        arg.result = mainFn.newTemp();
        arg.i64 = 5;
        mainEntry.body.push_back(arg);
        apollo::ir::Instr call;
        call.op = apollo::ir::Op::Call;
        call.type = apollo::ir::IrType::Void;
        call.result = mainFn.newTemp();
        call.text = "Bump";
        call.args.push_back(arg.result);
        mainEntry.body.push_back(call);
        mainEntry.term.kind = apollo::ir::TerminatorKind::Return;
        mainFn.blocks.push_back(std::move(mainEntry));

        apollo::ir::Function bump;
        bump.name = "Bump";
        bump.returnType = apollo::ir::IrType::Void;
        bump.params.push_back(apollo::ir::Param{"n", apollo::ir::IrType::I32});
        apollo::ir::BasicBlock bumpEntry;
        bumpEntry.label = "entry";
        bumpEntry.term.kind = apollo::ir::TerminatorKind::Return;
        bump.blocks.push_back(std::move(bumpEntry));

        module.functions.push_back(std::move(mainFn));
        module.functions.push_back(std::move(bump));

        const std::string tbc = apollo::codegen::emitTbc(module, diagnostics);
        if (diagnostics.errorCount() != 0 || tbc.empty()) {
            return fail("user Call IR should emit with zero diagnostics");
        }
        if (!contains(tbc, "CALL Bump") || !contains(tbc, "Bump:") ||
            !contains(tbc, "STORE_VAR Bump$n") || !contains(tbc, "RETURN")) {
            return fail("user Call .tbc missing CALL / Bump: / param store / RETURN");
        }
    }

    return 0;
}
