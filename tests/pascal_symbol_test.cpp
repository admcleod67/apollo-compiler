#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/AstDump.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/SymbolTable.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "pascal_symbol_test: " << message << '\n';
    return 1;
}

struct ScanParseSymbols {
    apollo::common::SourceFile source;
    apollo::common::DiagnosticEngine diagnostics;
    apollo::pascal::TokenStream tokens;
    std::unique_ptr<apollo::pascal::ast::Program> program;

    explicit ScanParseSymbols(std::string path, std::string text)
        : source(apollo::common::SourceFile::fromString(std::move(path), std::move(text))),
          diagnostics(source), tokens(apollo::pascal::scan(source, diagnostics)),
          program(apollo::pascal::parse(source, tokens, diagnostics)) {
        if (program) {
            apollo::pascal::buildSymbolTable(*program, diagnostics);
        }
    }
};

} // namespace

int main() {
    // Duplicate var in one scope (two entries in one var section)
    {
        ScanParseSymbols run("dup.pas", "program P; var i: integer; i: integer; begin end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("duplicate var should diagnose");
        }
    }

    // Clean program with builtins — writeln must not conflict
    {
        ScanParseSymbols run("hello.pas",
                             "program Hello; begin writeln('Hello, Gemini!'); end.");
        if (run.diagnostics.errorCount() != 0) {
            return fail("clean writeln program should have no symbol errors");
        }
    }

    // User procedure writeln clashes with builtin
    {
        ScanParseSymbols run("clash.pas",
                             "program P; procedure writeln; begin end; begin end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("procedure writeln should clash with builtin");
        }
    }

    // AST dump non-empty
    {
        ScanParseSymbols run("dump.pas", "program Hello; begin writeln('x'); end.");
        if (!run.program) {
            return fail("dump fixture should parse");
        }
        std::ostringstream out;
        apollo::pascal::writeAstDump(out, *run.program);
        const std::string dump = out.str();
        if (dump.find("Program Hello") == std::string::npos ||
            dump.find("CallStmt writeln") == std::string::npos) {
            return fail("AST dump missing expected labels");
        }
    }

    // Recovery: two independent syntax errors
    {
        apollo::common::SourceFile source = apollo::common::SourceFile::fromString(
            "multierr.pas", "program P; begin x := ; y := ; end.");
        apollo::common::DiagnosticEngine diagnostics(source);
        const auto tokens = apollo::pascal::scan(source, diagnostics);
        const auto program = apollo::pascal::parse(source, tokens, diagnostics);
        if (diagnostics.errorCount() < 2) {
            return fail("two syntax errors should produce >= 2 diagnostics");
        }
        if (!program) {
            return fail("multi-error program should still form a Program root");
        }
    }

    return 0;
}
