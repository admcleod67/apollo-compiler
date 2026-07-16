#include "apollo/ir/Ir.hpp"
#include "apollo/ir/IrDump.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "apollo_ir_test: " << message << '\n';
    return 1;
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    // Minimal module: const + return
    {
        apollo::ir::Module module;
        module.name = "demo";

        apollo::ir::Function mainFn;
        mainFn.name = "main";
        mainFn.returnType = apollo::ir::IrType::I32;

        apollo::ir::BasicBlock entry;
        entry.label = "entry";

        apollo::ir::Instr constInstr;
        constInstr.op = apollo::ir::Op::ConstI32;
        constInstr.type = apollo::ir::IrType::I32;
        constInstr.result = mainFn.newTemp();
        constInstr.i64 = 42;
        entry.body.push_back(constInstr);

        apollo::ir::Terminator ret;
        ret.kind = apollo::ir::TerminatorKind::Return;
        ret.value = constInstr.result;
        entry.term = ret;

        mainFn.blocks.push_back(std::move(entry));
        module.functions.push_back(std::move(mainFn));

        std::ostringstream out;
        apollo::ir::writeIrDump(out, module);
        const std::string dump = out.str();

        if (!contains(dump, "Module demo") || !contains(dump, "Function main") ||
            !contains(dump, "const.i32 42") || !contains(dump, "return %0")) {
            return fail("minimal module dump missing expected labels");
        }
    }

    // CallRuntime @writeln shape for Stage 2
    {
        apollo::ir::Module module;
        module.name = "hello";

        apollo::ir::Function mainFn;
        mainFn.name = "main";
        mainFn.returnType = apollo::ir::IrType::Void;

        apollo::ir::BasicBlock entry;
        entry.label = "entry";

        apollo::ir::Instr msg;
        msg.op = apollo::ir::Op::ConstString;
        msg.type = apollo::ir::IrType::StringRef;
        msg.result = mainFn.newTemp();
        msg.text = "Hello";
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

        std::ostringstream out;
        apollo::ir::writeIrDump(out, module);
        const std::string dump = out.str();

        if (!contains(dump, "call.runtime @writeln") || !contains(dump, "%0") ||
            !contains(dump, "const.string 'Hello'")) {
            return fail("writeln runtime call dump missing expected labels");
        }
    }

    return 0;
}
