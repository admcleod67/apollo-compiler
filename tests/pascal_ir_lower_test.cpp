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
    apollo::pascal::TokenStream tokens;
    std::unique_ptr<apollo::pascal::ast::Program> program;
    std::unique_ptr<apollo::pascal::SymbolTable> symbols;
    std::string dump;

    explicit ScanAnalyseLower(std::string path, std::string text)
        : source(apollo::common::SourceFile::fromString(std::move(path), std::move(text))),
          diagnostics(source), tokens(apollo::pascal::scan(source, diagnostics)),
          program(apollo::pascal::parse(source, tokens, diagnostics)) {
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

    // Unsupported construct (if) should diagnose rather than crash.
    {
        ScanAnalyseLower run("ifstmt.pas",
                             "program HasIf;\n"
                             "var\n"
                             "  x: integer;\n"
                             "begin\n"
                             "  if x > 0 then x := 1;\n"
                             "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("if statement should diagnose as not lowered until Stage 3");
        }
    }

    return 0;
}
