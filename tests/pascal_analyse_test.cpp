#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/Analyse.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/Type.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "pascal_analyse_test: " << message << '\n';
    return 1;
}

struct ScanAnalyse {
    apollo::common::SourceFile source;
    apollo::common::DiagnosticEngine diagnostics;
    apollo::pascal::TokenStream tokens;
    std::unique_ptr<apollo::pascal::ast::Program> program;

    explicit ScanAnalyse(std::string path, std::string text)
        : source(apollo::common::SourceFile::fromString(std::move(path), std::move(text))),
          diagnostics(source), tokens(apollo::pascal::scan(source, diagnostics)),
          program(apollo::pascal::parse(source, tokens, diagnostics)) {
        if (program) {
            (void)apollo::pascal::analyse(*program, diagnostics);
        }
    }
};

apollo::pascal::ast::Expr *firstAssignRhs(apollo::pascal::ast::Program &program) {
    if (program.block.body.statements.empty()) {
        return nullptr;
    }
    auto &stmt = program.block.body.statements.front();
    if (stmt.kind != apollo::pascal::ast::StmtKind::Assign) {
        return nullptr;
    }
    return stmt.value.get();
}

} // namespace

int main() {
    // Undeclared identifier in assign RHS
    {
        ScanAnalyse run("undecl.pas", "program P; var i: integer; begin i := x; end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("undeclared identifier should diagnose");
        }
    }

    // 1 + 2 is Integer
    {
        ScanAnalyse run("addint.pas", "program P; var i: integer; begin i := 1 + 2; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("1+2 fixture should be clean");
        }
        apollo::pascal::ast::Expr *rhs = firstAssignRhs(*run.program);
        if (!rhs || rhs->kind != apollo::pascal::ast::ExprKind::Binary ||
            apollo::pascal::canonicalTag(rhs->type) != apollo::pascal::TypeTag::Integer) {
            return fail("1 + 2 should type as Integer");
        }
    }

    // 1 + 2.0 is Real
    {
        ScanAnalyse run("addreal.pas", "program P; var r: real; begin r := 1 + 2.0; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("1+2.0 fixture should be clean");
        }
        apollo::pascal::ast::Expr *rhs = firstAssignRhs(*run.program);
        if (!rhs || apollo::pascal::canonicalTag(rhs->type) != apollo::pascal::TypeTag::Real) {
            return fail("1 + 2.0 should type as Real");
        }
    }

    // 1 < 2 is Boolean
    {
        ScanAnalyse run("rel.pas",
                        "program P; var b: boolean; begin b := 1 < 2; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("1<2 fixture should be clean");
        }
        apollo::pascal::ast::Expr *rhs = firstAssignRhs(*run.program);
        if (!rhs || apollo::pascal::canonicalTag(rhs->type) != apollo::pascal::TypeTag::Boolean) {
            return fail("1 < 2 should type as Boolean");
        }
    }

    // true and false is Boolean
    {
        ScanAnalyse run("boolop.pas",
                        "program P; var b: boolean; begin b := true and false; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("true and false fixture should be clean");
        }
        apollo::pascal::ast::Expr *rhs = firstAssignRhs(*run.program);
        if (!rhs || apollo::pascal::canonicalTag(rhs->type) != apollo::pascal::TypeTag::Boolean) {
            return fail("true and false should type as Boolean");
        }
    }

    // Bad operands diagnose; second independent error still reported
    {
        ScanAnalyse run("badops.pas",
                        "program P; var i: integer; begin i := 1 + true; i := nope; end.");
        if (run.diagnostics.errorCount() < 2) {
            return fail("bad operands and undeclared should yield >= 2 errors");
        }
    }

    // Nested procedure local resolves while scope is open
    {
        ScanAnalyse run("nested.pas",
                        "program P; procedure Q; var x: integer; begin x := x + 1; end; "
                        "begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("nested local fixture should be clean");
        }
        if (run.program->block.subprograms.empty() || !run.program->block.subprograms[0].block) {
            return fail("nested fixture missing procedure block");
        }
        auto &body = run.program->block.subprograms[0].block->body;
        if (body.statements.empty() || body.statements[0].kind != apollo::pascal::ast::StmtKind::Assign ||
            !body.statements[0].value) {
            return fail("nested fixture missing assign");
        }
        auto *rhs = body.statements[0].value.get();
        if (!rhs || apollo::pascal::canonicalTag(rhs->type) != apollo::pascal::TypeTag::Integer) {
            return fail("nested local x + 1 should be Integer");
        }
    }

    // examples/hello.pas (inline)
    {
        ScanAnalyse run("hello.pas",
                        "program Hello;\n"
                        "begin\n"
                        "  writeln('Hello, Gemini!');\n"
                        "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("hello.pas should analyse clean");
        }
    }

    // examples/count.pas (inline)
    {
        ScanAnalyse run("count.pas",
                        "program Count;\n"
                        "var\n"
                        "  i: integer;\n"
                        "begin\n"
                        "  for i := 1 to 10 do\n"
                        "    writeln(i);\n"
                        "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("count.pas should analyse clean");
        }
    }

    // Assign real to boolean
    {
        ScanAnalyse run("badassign.pas",
                        "program P; var b: boolean; begin b := 1.5; end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("real to boolean assignment should diagnose");
        }
    }

    // Non-boolean if condition
    {
        ScanAnalyse run("badif.pas", "program P; begin if 1 then writeln; end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("non-boolean if condition should diagnose");
        }
    }

    // for with boolean control variable
    {
        ScanAnalyse run("badfor.pas",
                        "program P; var b: boolean; begin for b := 1 to 10 do writeln; end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("boolean for control should diagnose");
        }
    }

    // writeln(true) not printable
    {
        ScanAnalyse run("badwrite.pas", "program P; begin writeln(true); end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("writeln(true) should diagnose");
        }
    }

    // User procedure wrong arity / bad arg type
    {
        ScanAnalyse run("badcall.pas",
                        "program P; procedure Q(x: integer); begin end; "
                        "begin Q; Q(1.0); end.");
        if (run.diagnostics.errorCount() < 2) {
            return fail("wrong arity and real arg should yield >= 2 errors");
        }
    }

    return 0;
}
