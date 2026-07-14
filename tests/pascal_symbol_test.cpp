#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/AstDump.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/SymbolTable.hpp"
#include "apollo/pascal/Type.hpp"

#include <iostream>
#include <memory>
#include <optional>
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
    std::optional<apollo::pascal::SymbolTable> symbols;

    explicit ScanParseSymbols(std::string path, std::string text)
        : source(apollo::common::SourceFile::fromString(std::move(path), std::move(text))),
          diagnostics(source), tokens(apollo::pascal::scan(source, diagnostics)),
          program(apollo::pascal::parse(source, tokens, diagnostics)) {
        if (program) {
            symbols = apollo::pascal::buildSymbolTable(*program, diagnostics);
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

    // var i: integer → Integer type
    {
        ScanParseSymbols run("intvar.pas", "program P; var i: integer; begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.symbols) {
            return fail("integer var fixture should be clean");
        }
        const auto *sym = run.symbols->lookup("i");
        if (!sym || sym->kind != apollo::pascal::SymbolKind::Var ||
            apollo::pascal::canonicalTag(sym->type) != apollo::pascal::TypeTag::Integer) {
            return fail("var i should be Integer");
        }
        if (!run.symbols->lookup("integer") ||
            run.symbols->lookup("integer")->kind != apollo::pascal::SymbolKind::Type) {
            return fail("predefined integer type should be visible");
        }
    }

    // type T = integer; var x: T → peels to Integer
    {
        ScanParseSymbols run("alias.pas",
                             "program P; type T = integer; var x: T; begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.symbols) {
            return fail("alias fixture should be clean");
        }
        const auto *x = run.symbols->lookup("x");
        if (!x || apollo::pascal::canonicalTag(x->type) != apollo::pascal::TypeTag::Integer) {
            return fail("var x: T should peel to Integer");
        }
        const auto *t = run.symbols->lookup("T");
        if (!t || t->kind != apollo::pascal::SymbolKind::Type || !t->type ||
            t->type->tag != apollo::pascal::TypeTag::Alias) {
            return fail("type T should be an Alias");
        }
    }

    // array [1..10] of integer
    {
        ScanParseSymbols run("arr.pas",
                             "program P; var a: array [1..10] of integer; begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.symbols) {
            return fail("array fixture should be clean");
        }
        const auto *a = run.symbols->lookup("a");
        if (!a || !a->type || a->type->tag != apollo::pascal::TypeTag::Array ||
            apollo::pascal::canonicalTag(a->type->element) != apollo::pascal::TypeTag::Integer) {
            return fail("array a should have Integer elements");
        }
    }

    // unknown type name
    {
        ScanParseSymbols run("badtype.pas", "program P; var z: nope; begin end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("unknown type name should diagnose");
        }
    }

    // const N = 10 → Integer
    {
        ScanParseSymbols run("constlit.pas", "program P; const N = 10; begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.symbols) {
            return fail("const literal fixture should be clean");
        }
        const auto *n = run.symbols->lookup("N");
        if (!n || n->kind != apollo::pascal::SymbolKind::Const ||
            apollo::pascal::canonicalTag(n->type) != apollo::pascal::TypeTag::Integer) {
            return fail("const N = 10 should be Integer");
        }
    }

    return 0;
}
