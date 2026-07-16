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

    // count.pas is multi-block — Stage 2 must diagnose.
    {
        ScanAnalyseLowerEmit run("count.pas",
                                 "program Count;\n"
                                 "var\n"
                                 "  i: integer;\n"
                                 "begin\n"
                                 "  for i := 1 to 10 do\n"
                                 "    writeln(i);\n"
                                 "end.\n");
        if (run.diagnostics.errorCount() == 0 || !run.tbc.empty()) {
            return fail("count.pas multi-block emit should diagnose in Stage 2");
        }
    }

    return 0;
}
