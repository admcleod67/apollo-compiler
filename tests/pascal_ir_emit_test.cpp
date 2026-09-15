#include "apollo/codegen/Emit.hpp"
#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/Analyse.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/ir/Lower.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "pascal_ir_emit_test: " << message << '\n';
    return 1;
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

struct ScanAnalyseLowerEmit {
    apollo::common::SourceFile source;
    apollo::common::DiagnosticEngine diagnostics;
    apollo::pascal::TokenStream tokens;
    std::unique_ptr<apollo::pascal::ast::Program> program;
    std::unique_ptr<apollo::pascal::SymbolTable> symbols;
    std::string tbc;

    explicit ScanAnalyseLowerEmit(std::string path, std::string text)
        : source(apollo::common::SourceFile::fromString(std::move(path), std::move(text))),
          diagnostics(source), tokens(apollo::pascal::scan(source, diagnostics)),
          program(apollo::pascal::parse(source, tokens, diagnostics)) {
        if (!program) {
            return;
        }
        symbols = std::make_unique<apollo::pascal::SymbolTable>(
            apollo::pascal::analyse(*program, diagnostics));
        if (diagnostics.errorCount() != 0) {
            return;
        }
        const apollo::ir::Module module =
            apollo::pascal::ir::lowerToIr(*program, *symbols, diagnostics);
        if (diagnostics.errorCount() != 0) {
            return;
        }
        tbc = apollo::codegen::emitTbc(module, diagnostics);
    }
};

} // namespace

int main() {
    // examples/hello.pas (inline)
    {
        ScanAnalyseLowerEmit run("hello.pas",
                                 "program Hello;\n"
                                 "begin\n"
                                 "  writeln('Hello, Gemini!');\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("hello.pas should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "Hello, Gemini") || !contains(run.tbc, "PRINT_") ||
            !contains(run.tbc, "HALT")) {
            return fail("hello.pas .tbc missing string / PRINT_ / HALT");
        }
    }

    // Synthetic straight-line: consts, vars, add, and/or, writeln.
    {
        ScanAnalyseLowerEmit run("straight.pas",
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
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("straight-line fixture should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "ADD") || !contains(run.tbc, "STORE_VAR main$") ||
            !contains(run.tbc, "PRINT_VAL") || !contains(run.tbc, "PRINT_EOL")) {
            return fail("straight-line .tbc missing ADD / STORE_VAR / writeln binding");
        }
    }

    // readln(n): INPUT_ then STORE_VAR.
    {
        ScanAnalyseLowerEmit run("readln.pas",
                                 "program ReadIt;\n"
                                 "var\n"
                                 "  n: integer;\n"
                                 "begin\n"
                                 "  readln(n);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("readln fixture should emit with zero diagnostics");
        }
        const std::size_t inputPos = run.tbc.find("INPUT_INT");
        const std::size_t storePos = run.tbc.find("STORE_VAR main$n");
        if (inputPos == std::string::npos || storePos == std::string::npos ||
            storePos < inputPos) {
            return fail("readln .tbc missing INPUT_INT followed by STORE_VAR main$n");
        }
    }

    // examples/count.pas (inline): for-loop CFG.
    {
        ScanAnalyseLowerEmit run("count.pas",
                                 "program Count;\n"
                                 "var\n"
                                 "  i: integer;\n"
                                 "begin\n"
                                 "  for i := 1 to 10 do\n"
                                 "    writeln(i);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("count.pas should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "main$for.head") || !contains(run.tbc, "LE") ||
            !contains(run.tbc, "JZ") || !contains(run.tbc, "JUMP") ||
            !contains(run.tbc, "HALT")) {
            return fail("count.pas .tbc missing for.head / LE / JZ / JUMP / HALT");
        }
    }

    // One-level procedure with param, called from main.
    {
        ScanAnalyseLowerEmit run("subprog.pas",
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
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("subprogram fixture should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "Bump:") || !contains(run.tbc, "CALL Bump") ||
            !contains(run.tbc, "STORE_VAR Bump$")) {
            return fail("subprogram .tbc missing Bump: / CALL Bump / STORE_VAR Bump$");
        }
    }

    // Function result: G := 1; i := G;
    {
        ScanAnalyseLowerEmit run("funcresult.pas",
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
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("function-result fixture should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "CALL G") || !contains(run.tbc, "RETURN") ||
            !contains(run.tbc, "G:")) {
            return fail("function-result .tbc missing CALL G / RETURN / G:");
        }
    }

    // real literal → PUSH_FLT
    {
        ScanAnalyseLowerEmit run("real.pas",
                                 "program RealDemo;\n"
                                 "var\n"
                                 "  x: real;\n"
                                 "begin\n"
                                 "  x := 3.5;\n"
                                 "  writeln(x);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("real fixture should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "PUSH_FLT")) {
            return fail("real fixture .tbc missing PUSH_FLT");
        }
    }

    // integer mod → DIV/MUL/SUB sequence
    {
        ScanAnalyseLowerEmit run("mod.pas",
                                 "program ModDemo;\n"
                                 "begin\n"
                                 "  writeln(10 mod 3);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("mod fixture should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "DIV") || !contains(run.tbc, "MUL") ||
            !contains(run.tbc, "SUB")) {
            return fail("mod fixture .tbc missing DIV/MUL/SUB remainder sequence");
        }
    }

    // array index: DIM_ARRAY + LOAD_ARR / STORE_ARR
    {
        ScanAnalyseLowerEmit run("arr.pas",
                                 "program ArrDemo;\n"
                                 "var\n"
                                 "  a: array [0..2] of integer;\n"
                                 "begin\n"
                                 "  a[1] := 7;\n"
                                 "  writeln(a[1]);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("array fixture should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "DIM_ARRAY") || !contains(run.tbc, "STORE_ARR") ||
            !contains(run.tbc, "LOAD_ARR")) {
            return fail("array fixture .tbc missing DIM_ARRAY / STORE_ARR / LOAD_ARR");
        }
    }

    // Negative lower bound sizes the array from the range, not from a default.
    {
        ScanAnalyseLowerEmit run("arrneg.pas",
                                 "program ArrNeg;\n"
                                 "var\n"
                                 "  a: array [-3..3] of integer;\n"
                                 "begin\n"
                                 "  a[-3] := 7;\n"
                                 "  writeln(a[-3]);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("array [-3..3] should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "PUSH_INT 7\n    DIM_ARRAY main$a")) {
            return fail("array [-3..3] should DIM 7 elements");
        }
        if (!contains(run.tbc, "PUSH_INT -4\n    SUB")) {
            return fail("array [-3..3] index remap should subtract lo - 1");
        }
    }

    // Parenthesised bound.
    {
        ScanAnalyseLowerEmit run("arrgroup.pas",
                                 "program ArrGroup;\n"
                                 "var\n"
                                 "  a: array [1..(4)] of integer;\n"
                                 "begin\n"
                                 "  a[4] := 1;\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("array [1..(4)] should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "PUSH_INT 4\n    DIM_ARRAY main$a")) {
            return fail("array [1..(4)] should DIM 4 elements");
        }
    }

    // Program-level const bound.
    {
        ScanAnalyseLowerEmit run("arrconst.pas",
                                 "program ArrConst;\n"
                                 "const\n"
                                 "  n = 4;\n"
                                 "var\n"
                                 "  a: array [1..n] of integer;\n"
                                 "begin\n"
                                 "  a[4] := 1;\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("const array bound should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "PUSH_INT 4\n    DIM_ARRAY main$a")) {
            return fail("const array bound should DIM 4 elements");
        }
    }

    // Const bound declared inside a subprogram: its scope is gone by lowering time.
    {
        ScanAnalyseLowerEmit run("arrlocalconst.pas",
                                 "program ArrLocalConst;\n"
                                 "procedure p;\n"
                                 "const\n"
                                 "  n = 5;\n"
                                 "var\n"
                                 "  a: array [1..n] of integer;\n"
                                 "begin\n"
                                 "  a[5] := 3;\n"
                                 "end;\n"
                                 "begin\n"
                                 "  p;\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("subprogram-local const bound should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "PUSH_INT 5\n    DIM_ARRAY p$a")) {
            return fail("subprogram-local const bound should DIM 5 elements");
        }
    }

    // Subprogram-local type alias: also unresolvable post-analyse without the annotation.
    {
        ScanAnalyseLowerEmit run("localalias.pas",
                                 "program LocalAlias;\n"
                                 "procedure p;\n"
                                 "type\n"
                                 "  ti = integer;\n"
                                 "var\n"
                                 "  x: ti;\n"
                                 "begin\n"
                                 "  readln(x);\n"
                                 "  writeln(x);\n"
                                 "end;\n"
                                 "begin\n"
                                 "  p;\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("subprogram-local type alias should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "INPUT_INT")) {
            return fail("subprogram-local alias local should read as integer");
        }
    }

    // Pascal `/` is real division: integer operands must be widened, not truncated.
    {
        ScanAnalyseLowerEmit run("realdiv.pas",
                                 "program RealDiv;\n"
                                 "begin\n"
                                 "  writeln(1 / 2);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("1 / 2 should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "PUSH_FLT 1.0") || !contains(run.tbc, "PUSH_FLT 2.0")) {
            return fail("1 / 2 should push float operands");
        }
        if (contains(run.tbc, "PUSH_INT 1\n") || contains(run.tbc, "PUSH_INT 2\n")) {
            return fail("1 / 2 should not keep integer operands");
        }
    }

    // Integer variable assigned to a real, then used in real division.
    {
        ScanAnalyseLowerEmit run("realwiden.pas",
                                 "program RealWiden;\n"
                                 "var\n"
                                 "  x: real;\n"
                                 "  i: integer;\n"
                                 "begin\n"
                                 "  i := 7;\n"
                                 "  x := i;\n"
                                 "  writeln(i / 2);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("integer to real widening should emit with zero diagnostics");
        }
        // ConvertF64 widens by multiplying with 1.0 (Gemini has no int -> float opcode).
        if (!contains(run.tbc, "PUSH_FLT 1.0\n    MUL")) {
            return fail("widening should emit a PUSH_FLT 1.0 / MUL pair");
        }
    }

    // Real array element assigned an integer literal.
    {
        ScanAnalyseLowerEmit run("realarr.pas",
                                 "program RealArr;\n"
                                 "var\n"
                                 "  a: array [1..2] of real;\n"
                                 "begin\n"
                                 "  a[1] := 1;\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("real array element assign should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "PUSH_FLT 1.0")) {
            return fail("real array element should store a float");
        }
    }

    // Integer argument passed to a real parameter.
    {
        ScanAnalyseLowerEmit run("realparam.pas",
                                 "program RealParam;\n"
                                 "var\n"
                                 "  i: integer;\n"
                                 "function half(v: real): real;\n"
                                 "begin\n"
                                 "  half := v / 2;\n"
                                 "end;\n"
                                 "begin\n"
                                 "  i := 3;\n"
                                 "  writeln(half(i));\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("real parameter fixture should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "PUSH_FLT 1.0\n    MUL") || !contains(run.tbc, "CALL half")) {
            return fail("integer argument should widen before CALL");
        }
    }

    // readln into a real reads a line and parses it (no float input opcode).
    {
        ScanAnalyseLowerEmit run("realread.pas",
                                 "program RealRead;\n"
                                 "var\n"
                                 "  x: real;\n"
                                 "begin\n"
                                 "  readln(x);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("readln of a real should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "INPUT_STR\n    PUSH_FLT 1.0\n    MUL")) {
            return fail("readln of a real should parse the input line as a float");
        }
    }

    // Real literals keep full precision through emit.
    {
        ScanAnalyseLowerEmit run("realprec.pas",
                                 "program RealPrec;\n"
                                 "var\n"
                                 "  x: real;\n"
                                 "begin\n"
                                 "  x := 3.14159265358979;\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("real literal fixture should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "PUSH_FLT 3.14159265358979")) {
            return fail("real literal should not be truncated to six digits");
        }
    }

    // Whole-array assign lowers to MAT_COPY.
    {
        ScanAnalyseLowerEmit run("matcopy.pas",
                                 "program MatCopy;\n"
                                 "var\n"
                                 "  a, b: array [1..2] of integer;\n"
                                 "begin\n"
                                 "  a := b;\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("whole-array assign should emit");
        }
        if (!contains(run.tbc, "MAT_COPY main$a|main$b")) {
            return fail("whole-array assign should emit MAT_COPY");
        }
    }

    // Value array parameter: call site MAT_COPY, callee formal not loaded from stack.
    {
        ScanAnalyseLowerEmit run("arrparam.pas",
                                 "program ArrParam;\n"
                                 "var\n"
                                 "  src: array [1..2] of integer;\n"
                                 "procedure bump(a: array [1..2] of integer);\n"
                                 "begin\n"
                                 "  a[1] := 0;\n"
                                 "end;\n"
                                 "begin\n"
                                 "  bump(src);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("array value parameter should emit");
        }
        if (!contains(run.tbc, "MAT_COPY bump$a|main$src")) {
            return fail("array argument should MAT_COPY into callee formal");
        }
    }

    // Record fields use fn$base$field mangling.
    {
        ScanAnalyseLowerEmit run("recfield.pas",
                                 "program RecField;\n"
                                 "type\n"
                                 "  point = record x, y: integer; end;\n"
                                 "var\n"
                                 "  p: point;\n"
                                 "begin\n"
                                 "  p.x := 2;\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("record field store should emit");
        }
        if (!contains(run.tbc, "main$p$x")) {
            return fail("record field should mangle as main$p$x");
        }
    }

    // Case lowers to compare / branch chain.
    {
        ScanAnalyseLowerEmit run("caseemit.pas",
                                 "program CaseEmit;\n"
                                 "var\n"
                                 "  n: integer;\n"
                                 "begin\n"
                                 "  n := 2;\n"
                                 "  case n of\n"
                                 "    1: writeln(1);\n"
                                 "    2..4: writeln(2);\n"
                                 "  else\n"
                                 "    writeln(0)\n"
                                 "  end\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("case emit should succeed");
        }
        if (!contains(run.tbc, "EQ") || !contains(run.tbc, "JZ") || !contains(run.tbc, "JUMP")) {
            return fail("case should emit compare and branch opcodes");
        }
    }

    return 0;
}
