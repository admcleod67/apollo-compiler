#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "pascal_parser_test: " << message << '\n';
    return 1;
}

struct ScanParse {
    apollo::common::SourceFile source;
    apollo::common::DiagnosticEngine diagnostics;
    apollo::pascal::TokenStream tokens;
    std::unique_ptr<apollo::pascal::ast::Program> program;

    explicit ScanParse(std::string path, std::string text)
        : source(apollo::common::SourceFile::fromString(std::move(path), std::move(text))),
          diagnostics(source), tokens(apollo::pascal::scan(source, diagnostics)),
          program(apollo::pascal::parse(source, tokens, diagnostics)) {}
};

} // namespace

int main() {
    // Happy path: program Hello; begin end.
    {
        ScanParse run("ok.pas", "program Hello; begin end.");
        if (run.diagnostics.errorCount() != 0) {
            return fail("well-formed program should parse with no errors");
        }
        if (!run.program) {
            return fail("well-formed program should produce a Program AST");
        }
        if (run.program->name != "Hello") {
            return fail("program name should be Hello");
        }
        if (!run.program->block.body.statements.empty()) {
            return fail("Stage 1 compound statement should have no statements");
        }
        if (run.program->range.begin.line != 1 || run.program->range.begin.column != 1) {
            return fail("program range should start at 1:1");
        }
    }

    // Missing 'program'
    {
        ScanParse run("noprog.pas", "Hello; begin end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("missing 'program' should diagnose");
        }
        if (run.program) {
            return fail("missing 'program' should not form a Program root");
        }
    }

    // Missing final '.'
    {
        ScanParse run("nodot.pas", "program Hello; begin end");
        if (run.diagnostics.errorCount() == 0) {
            return fail("missing '.' should diagnose");
        }
        // Partial tree is acceptable; must not crash.
        if (!run.program) {
            return fail("missing '.' after a full block should still produce a Program");
        }
        if (run.program->name != "Hello") {
            return fail("partial program should retain the name");
        }
    }

    // Multi-line location spot-check
    {
        ScanParse run("lines.pas", "program Hello;\nbegin\nend.\n");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("multi-line skeleton should parse cleanly");
        }
        if (run.program->block.body.range.begin.line != 2) {
            return fail("begin should be on line 2");
        }
        if (run.program->block.body.range.end.line < 3) {
            return fail("compound range should extend through end on line 3");
        }
    }

    return 0;
}
