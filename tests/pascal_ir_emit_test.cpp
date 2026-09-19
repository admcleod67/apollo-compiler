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
    apollo::pascal::ScanResult scanned;
    std::unique_ptr<apollo::pascal::ast::Program> program;
    std::unique_ptr<apollo::pascal::SymbolTable> symbols;
    std::string tbc;

    explicit ScanAnalyseLowerEmit(std::string path, std::string text)
        : source(apollo::common::SourceFile::fromString(std::move(path), std::move(text))),
          diagnostics(source), scanned(apollo::pascal::scan(source, diagnostics)),
          program(apollo::pascal::parse(source, scanned.tokens, diagnostics)) {
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

    // integer mod → Gemini MOD opcode
    {
        ScanAnalyseLowerEmit run("mod.pas",
                                 "program ModDemo;\n"
                                 "begin\n"
                                 "  writeln(10 mod 3);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("mod fixture should emit with zero diagnostics");
        }
        if (!contains(run.tbc, "MOD")) {
            return fail("mod fixture .tbc missing MOD opcode");
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
        // ConvertF64 widens via Gemini COERCE_FLT.
        if (!contains(run.tbc, "COERCE_FLT")) {
            return fail("widening should emit COERCE_FLT");
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
        if (!contains(run.tbc, "COERCE_FLT") || !contains(run.tbc, "CALL half")) {
            return fail("integer argument should widen with COERCE_FLT before CALL");
        }
    }

    // readln into a real uses INPUT_FLT.
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
        if (!contains(run.tbc, "INPUT_FLT")) {
            return fail("readln of a real should emit INPUT_FLT");
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

    // Value array parameter: call site DIM + INIT + MAT_COPY (callee must not re-dim formal).
    // Old order copied into a missing/wiped formal; Gemini requires the destination to exist
    // first, and re-DIM after copy would discard the copied contents.
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
        const std::string dimInitCopy =
            "PUSH_INT 2\n    DIM_ARRAY bump$a\n    PUSH_INT 0\n    MAT_INIT bump$a\n"
            "    MAT_COPY bump$a|main$src";
        if (!contains(run.tbc, dimInitCopy)) {
            return fail("array argument should DIM then INIT then MAT_COPY at call site");
        }
        const auto callPos = run.tbc.find("CALL bump");
        const auto setupPos = run.tbc.find(dimInitCopy);
        if (callPos == std::string::npos || setupPos == std::string::npos || setupPos > callPos) {
            return fail("array argument setup must precede CALL");
        }
        const auto bumpLabel = run.tbc.find("bump:");
        if (bumpLabel == std::string::npos) {
            return fail("array parameter callee label missing");
        }
        const auto bumpDim = run.tbc.find("DIM_ARRAY bump$a", bumpLabel);
        if (bumpDim != std::string::npos) {
            return fail("callee must not DIM_ARRAY the array formal (call site owns dim/copy)");
        }
    }

    // Record array-field whole assign lowers to MAT_COPY.
    {
        ScanAnalyseLowerEmit run("recarr.pas",
                                 "program RecArr;\n"
                                 "type\n"
                                 "  box = record a: array [1..2] of integer; end;\n"
                                 "var\n"
                                 "  r, s: box;\n"
                                 "begin\n"
                                 "  r.a := s.a;\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("record array-field assign should emit");
        }
        if (!contains(run.tbc, "MAT_COPY main$r$a|main$s$a")) {
            return fail("record array-field assign should emit MAT_COPY main$r$a|main$s$a");
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

    // Stage 1 standard functions emit expected opcodes.
    {
        ScanAnalyseLowerEmit run("stdemit.pas",
                                 "program StdEmit;\n"
                                 "var\n"
                                 "  i: integer;\n"
                                 "  r: real;\n"
                                 "  c: char;\n"
                                 "  b: boolean;\n"
                                 "begin\n"
                                 "  i := ord('A');\n"
                                 "  c := chr(65);\n"
                                 "  i := abs(-3);\n"
                                 "  r := sqr(1.5);\n"
                                 "  b := odd(i);\n"
                                 "  i := trunc(r);\n"
                                 "  i := round(-1.5);\n"
                                 "  writeln(i, c);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("standard functions should emit");
        }
        if (!contains(run.tbc, "ABS_INT") || !contains(run.tbc, "COERCE_INT") ||
            !contains(run.tbc, "MUL") || !contains(run.tbc, "PRINT_CHAR")) {
            return fail("standard functions should emit ABS_INT / COERCE_INT / MUL / PRINT_CHAR");
        }
    }

    // Char literals and variables print as glyphs via PRINT_CHAR.
    {
        ScanAnalyseLowerEmit run("printchar.pas",
                                 "program PrintChar;\n"
                                 "var\n"
                                 "  c: char;\n"
                                 "begin\n"
                                 "  c := 'A';\n"
                                 "  writeln(c);\n"
                                 "  writeln('B');\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("char print fixture should emit");
        }
        if (!contains(run.tbc, "PRINT_CHAR")) {
            return fail("char write should emit PRINT_CHAR");
        }
    }

    // Stage 1b transcendentals emit CALL_FUNC into the shared math module.
    {
        ScanAnalyseLowerEmit run("mathemit.pas",
                                 "program MathEmit;\n"
                                 "var\n"
                                 "  r: real;\n"
                                 "  i: integer;\n"
                                 "begin\n"
                                 "  r := sqrt(9.0);\n"
                                 "  r := sin(0.0);\n"
                                 "  r := cos(0.0);\n"
                                 "  r := arctan(1.0);\n"
                                 "  r := ln(1.0);\n"
                                 "  r := exp(0.0);\n"
                                 "  i := 4;\n"
                                 "  r := sqrt(i);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() != 0 || run.tbc.empty()) {
            return fail("transcendental functions should emit");
        }
        if (!contains(run.tbc, "CALL_FUNC 6, 0, 1") || !contains(run.tbc, "CALL_FUNC 6, 1, 1") ||
            !contains(run.tbc, "CALL_FUNC 6, 2, 1") || !contains(run.tbc, "CALL_FUNC 6, 4, 1") ||
            !contains(run.tbc, "CALL_FUNC 6, 5, 1") || !contains(run.tbc, "CALL_FUNC 6, 6, 1")) {
            return fail("math builtins should emit CALL_FUNC 6,<id>,1");
        }
        if (!contains(run.tbc, "COERCE_FLT")) {
            return fail("integer sqrt argument should widen with COERCE_FLT");
        }
        if (contains(run.tbc, "CALL_FUNC 6, 3, 1")) {
            return fail("tan must not be emitted");
        }
    }

    return 0;
}
