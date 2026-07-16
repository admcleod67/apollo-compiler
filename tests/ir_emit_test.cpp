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

    // Multi-block module should diagnose (Stage 3).
    {
        apollo::common::SourceFile source =
            apollo::common::SourceFile::fromString("cfg.pas", "program P; begin end.");
        apollo::common::DiagnosticEngine diagnostics(source);

        apollo::ir::Module module;
        module.name = "P";
        apollo::ir::Function mainFn;
        mainFn.name = "main";
        mainFn.returnType = apollo::ir::IrType::Void;

        apollo::ir::BasicBlock a;
        a.label = "entry";
        a.term.kind = apollo::ir::TerminatorKind::Branch;
        a.term.target = "other";

        apollo::ir::BasicBlock b;
        b.label = "other";
        b.term.kind = apollo::ir::TerminatorKind::Return;

        mainFn.blocks.push_back(std::move(a));
        mainFn.blocks.push_back(std::move(b));
        module.functions.push_back(std::move(mainFn));

        const std::string tbc = apollo::codegen::emitTbc(module, diagnostics);
        if (diagnostics.errorCount() == 0 || !tbc.empty()) {
            return fail("multi-block IR should diagnose and produce no .tbc");
        }
    }

    return 0;
}
