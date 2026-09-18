#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/ir/IrDump.hpp"
#include "apollo/pascal/Analyse.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/ir/Lower.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "pascal_ir_lower_test: " << message << '\n';
    return 1;
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

struct ScanAnalyseLower {
    apollo::common::SourceFile source;
    apollo::common::DiagnosticEngine diagnostics;
    apollo::pascal::ScanResult scanned;
    std::unique_ptr<apollo::pascal::ast::Program> program;
    std::unique_ptr<apollo::pascal::SymbolTable> symbols;
    std::string dump;

    explicit ScanAnalyseLower(std::string path, std::string text)
        : source(apollo::common::SourceFile::fromString(std::move(path), std::move(text))),
          diagnostics(source), scanned(apollo::pascal::scan(source, diagnostics)),
          program(apollo::pascal::parse(source, scanned.tokens, diagnostics)) {
        if (!program) {
            return;
        }
        symbols = std::make_unique<apollo::pascal::SymbolTable>(
            apollo::pascal::analyse(*program, diagnostics));
        const apollo::ir::Module module =
            apollo::pascal::ir::lowerToIr(*program, *symbols, diagnostics);
        std::ostringstream out;
        apollo::ir::writeIrDump(out, module);
        dump = out.str();
    }
};

} // namespace

int main() {
    // Synthetic straight-line fixture: consts, vars, arithmetic, and/or, writeln.
    {
        ScanAnalyseLower run("straight.pas",
                             "program Straight;\n"
                             "const\n"
                             "  limit = 3;\n"
                             "var\n"
                             "  x: integer;\n"
                             "  ok: boolean;\n"
                             "begin\n"
                             "  x := 1 + limit;\n"
                             "  ok := (x > 0) and (x < 10);\n"
                             "  ok := ok or false;\n"
                             "  writeln('x = ', x);\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("straight-line fixture should lower with zero diagnostics");
        }
        if (!contains(run.dump, "Module Straight") || !contains(run.dump, "Function main")) {
            return fail("dump missing module/function labels");
        }
        if (!contains(run.dump, "const.i32 1") || !contains(run.dump, "const.i32 3")) {
            return fail("dump missing inlined const literals");
        }
        if (!contains(run.dump, "add") || !contains(run.dump, "store.local local[0]")) {
            return fail("dump missing add / store.local for x assignment");
        }
        if (!contains(run.dump, " and ") || !contains(run.dump, " or ")) {
            return fail("dump missing and/or ops");
        }
        if (!contains(run.dump, "call.runtime @writeln")) {
            return fail("dump missing call.runtime @writeln");
        }
    }

    // examples/hello.pas shape: no vars, single writeln with a decoded string literal.
    {
        ScanAnalyseLower run("hello.pas",
                             "program Hello;\n"
                             "begin\n"
                             "  writeln('Hello, Gemini!');\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("hello.pas should lower with zero diagnostics");
        }
        if (!contains(run.dump, "call.runtime @writeln") ||
            !contains(run.dump, "const.string 'Hello, Gemini!'")) {
            return fail("hello.pas dump missing decoded writeln string");
        }
    }

    // readln(n) shape: call.runtime @readln immediately followed by store.local.
    {
        ScanAnalyseLower run("readln.pas",
                             "program ReadIt;\n"
                             "var\n"
                             "  n: integer;\n"
                             "begin\n"
                             "  readln(n);\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("readln fixture should lower with zero diagnostics");
        }
        const std::string callMarker = "call.runtime @readln";
        const std::string storeMarker = "store.local local[0]";
        const std::size_t callPos = run.dump.find(callMarker);
        const std::size_t storePos = run.dump.find(storeMarker);
        if (callPos == std::string::npos || storePos == std::string::npos ||
            storePos < callPos) {
            return fail("readln dump missing call.runtime @readln followed by store.local");
        }
    }

    // if/else lowers to a multi-block CFG with a branch.if.
    {
        ScanAnalyseLower run("ifelse.pas",
                             "program HasIf;\n"
                             "var\n"
                             "  x: integer;\n"
                             "begin\n"
                             "  if x > 0 then x := 1 else x := 2;\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("if/else fixture should lower with zero diagnostics");
        }
        if (!contains(run.dump, "Block if.then.") || !contains(run.dump, "Block if.else.") ||
            !contains(run.dump, "Block if.end.")) {
            return fail("if/else dump missing then/else/end blocks");
        }
        if (!contains(run.dump, "branch.if")) {
            return fail("if/else dump missing branch.if");
        }
    }

    // while lowers to head/body/end blocks that loop back to head.
    {
        ScanAnalyseLower run("whileloop.pas",
                             "program HasWhile;\n"
                             "var\n"
                             "  x: integer;\n"
                             "begin\n"
                             "  while x < 10 do x := x + 1;\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("while fixture should lower with zero diagnostics");
        }
        if (!contains(run.dump, "Block while.head.") || !contains(run.dump, "Block while.body.") ||
            !contains(run.dump, "Block while.end.")) {
            return fail("while dump missing head/body/end blocks");
        }
        if (!contains(run.dump, "branch while.head.")) {
            return fail("while dump missing loop-back branch to head");
        }
    }

    // repeat...until loops while the condition is false.
    {
        ScanAnalyseLower run("repeatloop.pas",
                             "program HasRepeat;\n"
                             "var\n"
                             "  x: integer;\n"
                             "begin\n"
                             "  repeat x := x + 1 until x >= 10;\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("repeat fixture should lower with zero diagnostics");
        }
        if (!contains(run.dump, "Block repeat.body.") || !contains(run.dump, "Block repeat.end.")) {
            return fail("repeat dump missing body/end blocks");
        }
        if (!contains(run.dump, "branch.if")) {
            return fail("repeat dump missing branch.if for the until condition");
        }
    }

    // examples/count.pas (inline): for + writeln, the Stage 3 golden control-flow fixture.
    {
        ScanAnalyseLower run("count.pas",
                             "program Count;\n"
                             "var\n"
                             "  i: integer;\n"
                             "begin\n"
                             "  for i := 1 to 10 do\n"
                             "    writeln(i);\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("count.pas should lower with zero diagnostics");
        }
        if (!contains(run.dump, "Block for.head.") || !contains(run.dump, "Block for.body.") ||
            !contains(run.dump, "Block for.end.")) {
            return fail("count.pas dump missing for.head/body/end blocks");
        }
        if (!contains(run.dump, "cmp.le") || !contains(run.dump, "call.runtime @writeln")) {
            return fail("count.pas dump missing cmp.le / call.runtime @writeln");
        }
    }

    // One-level subprogram: procedure with a param + local var, called from main.
    {
        ScanAnalyseLower run("subprog.pas",
                             "program HasProc;\n"
                             "procedure Bump(n: integer);\n"
                             "var\n"
                             "  doubled: integer;\n"
                             "begin\n"
                             "  doubled := n + n;\n"
                             "  writeln(doubled);\n"
                             "end;\n"
                             "begin\n"
                             "  Bump(5);\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("subprogram fixture should lower with zero diagnostics");
        }
        if (!contains(run.dump, "Function Bump")) {
            return fail("dump missing lowered subprogram Function Bump");
        }
        if (!contains(run.dump, "param[0]")) {
            return fail("dump missing param[0] reference inside Bump");
        }
    }

    // Function result: assigning to the function's own name sets the return value.
    {
        ScanAnalyseLower run("funcresult.pas",
                             "program HasFunc;\n"
                             "var\n"
                             "  i: integer;\n"
                             "function G: integer;\n"
                             "begin\n"
                             "  G := 1;\n"
                             "end;\n"
                             "begin\n"
                             "  i := G;\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("function-result fixture should lower with zero diagnostics");
        }
        if (!contains(run.dump, "Function G -> i32")) {
            return fail("dump missing Function G -> i32");
        }
        const std::size_t funcPos = run.dump.find("Function G -> i32");
        const std::size_t constOnePos = run.dump.find("const.i32 1", funcPos);
        const std::size_t returnPos = run.dump.find("return", funcPos);
        if (constOnePos == std::string::npos || returnPos == std::string::npos ||
            returnPos < constOnePos) {
            return fail("G's return should follow its assigned const.i32 1 result");
        }
    }

    // Accessing an enclosing-scope (outer) variable from within a subprogram is diagnosed.
    {
        ScanAnalyseLower run("outerscope.pas",
                             "program HasOuterAccess;\n"
                             "var\n"
                             "  total: integer;\n"
                             "procedure Bad;\n"
                             "begin\n"
                             "  total := total + 1;\n"
                             "end;\n"
                             "begin\n"
                             "  Bad;\n"
                             "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("accessing an outer-scope var from a subprogram should diagnose");
        }
    }

    // Case lowers to labeled test/arm/merge blocks.
    {
        ScanAnalyseLower run("caselower.pas",
                             "program CaseLower;\n"
                             "var n: integer;\n"
                             "begin\n"
                             "  case n of\n"
                             "    1: n := 1;\n"
                             "    2: n := 2\n"
                             "  end\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("case should lower cleanly");
        }
        if (!contains(run.dump, "case.test.") || !contains(run.dump, "case.arm.") ||
            !contains(run.dump, "case.merge.")) {
            return fail("case IR dump missing test/arm/merge labels");
        }
    }

    // Stage 1 standard functions lower to IR ops.
    {
        ScanAnalyseLower run("stdfuncs.pas",
                             "program StdFuncs;\n"
                             "var\n"
                             "  i: integer;\n"
                             "  r: real;\n"
                             "  c: char;\n"
                             "  b: boolean;\n"
                             "begin\n"
                             "  i := ord('A');\n"
                             "  c := chr(65);\n"
                             "  i := succ(1);\n"
                             "  i := pred(2);\n"
                             "  b := odd(3);\n"
                             "  i := abs(-4);\n"
                             "  r := abs(-1.5);\n"
                             "  i := sqr(3);\n"
                             "  i := trunc(3.2);\n"
                             "  i := round(1.5);\n"
                             "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("standard functions should lower cleanly");
        }
        if (!contains(run.dump, "copy") || !contains(run.dump, "abs") ||
            !contains(run.dump, "convert.i32") || !contains(run.dump, "mod") ||
            !contains(run.dump, "mul")) {
            return fail("standard function IR dump missing expected ops");
        }
        if (!contains(run.dump, "round.pos.") || !contains(run.dump, "round.merge.")) {
            return fail("round should lower to a BranchIf diamond");
        }
    }

    return 0;
}
