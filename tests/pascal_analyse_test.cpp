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
    apollo::pascal::ScanResult scanned;
    std::unique_ptr<apollo::pascal::ast::Program> program;

    explicit ScanAnalyse(std::string path, std::string text)
        : source(apollo::common::SourceFile::fromString(std::move(path), std::move(text))),
          diagnostics(source), scanned(apollo::pascal::scan(source, diagnostics)),
          program(apollo::pascal::parse(source, scanned.tokens, diagnostics)) {
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

    // Duplicate procedure must not merge signatures onto the first symbol
    {
        ScanAnalyse run("dupsug.pas",
                        "program P; procedure Q; begin end; "
                        "procedure Q(x: integer); begin end; "
                        "begin Q(1); end.");
        if (run.diagnostics.errorCount() < 2) {
            return fail("duplicate procedure + Q(1) should yield >= 2 errors");
        }
    }

    // Bare function with parameters is a 0-arg call (wrong arity)
    {
        ScanAnalyse run("barefn.pas",
                        "program P; var i: integer; "
                        "function F(x: integer): integer; begin F := x; end; "
                        "begin i := F; end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("bare F with required params should diagnose");
        }
    }

    // Bare zero-param function is a valid 0-arg call
    {
        ScanAnalyse run("barefn0.pas",
                        "program P; var i: integer; "
                        "function G: integer; begin G := 1; end; "
                        "begin i := G; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("bare zero-param function should be clean");
        }
        apollo::pascal::ast::Expr *rhs = firstAssignRhs(*run.program);
        if (!rhs || apollo::pascal::canonicalTag(rhs->type) != apollo::pascal::TypeTag::Integer) {
            return fail("i := G should type as Integer");
        }
    }

    // Compatible whole-array assignment (value copy via MAT_COPY).
    {
        ScanAnalyse run("arrayok.pas",
                        "program P;\n"
                        "var\n"
                        "  a: array [1..10] of integer;\n"
                        "  b: array [1..10] of integer;\n"
                        "begin\n"
                        "  a := b;\n"
                        "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("compatible whole-array assignment should be clean");
        }
    }

    // Incompatible array assignment (different element types)
    {
        ScanAnalyse run("arraybad.pas",
                        "program P;\n"
                        "var\n"
                        "  a: array [1..10] of integer;\n"
                        "  b: array [1..10] of real;\n"
                        "begin\n"
                        "  a := b;\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("array of integer := array of real should diagnose");
        }
    }

    // Indexed assign and load type-check.
    {
        ScanAnalyse run("arrayidx.pas",
                        "program P;\n"
                        "var\n"
                        "  a: array [0..2] of integer;\n"
                        "  i: integer;\n"
                        "begin\n"
                        "  a[1] := 42;\n"
                        "  i := a[1];\n"
                        "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("indexed array assign/load should be clean");
        }
    }

    // Non-const array bound.
    {
        ScanAnalyse run("arraybound.pas",
                        "program P;\n"
                        "var\n"
                        "  n: integer;\n"
                        "  a: array [1..n] of integer;\n"
                        "begin\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("non-const array bound should diagnose");
        }
    }

    // Array bound beyond the VM's 32-bit integer range.
    {
        ScanAnalyse run("arraybig.pas",
                        "program P;\n"
                        "var\n"
                        "  a: array [1..3000000000] of integer;\n"
                        "begin\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("out-of-range array bound should diagnose");
        }
    }

    // Value array parameters (Stage 2).
    {
        ScanAnalyse run("arrayparam.pas",
                        "program P;\n"
                        "type\n"
                        "  t = array [1..3] of integer;\n"
                        "var\n"
                        "  arr: t;\n"
                        "procedure q(v: t);\n"
                        "begin\n"
                        "end;\n"
                        "begin\n"
                        "  q(arr);\n"
                        "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("value array parameter call should be clean");
        }
    }

    // Flat record field access and whole-record assign.
    {
        ScanAnalyse run("recordok.pas",
                        "program RecDemo;\n"
                        "type\n"
                        "  point = record x, y: integer; end;\n"
                        "var\n"
                        "  p, q: point;\n"
                        "begin\n"
                        "  p.x := 1;\n"
                        "  q := p;\n"
                        "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("record field and whole-record assign should be clean");
        }
    }

    // Record array-field assign requires matching bounds.
    {
        ScanAnalyse run("recarrbad.pas",
                        "program P;\n"
                        "type\n"
                        "  a2 = record a: array [1..2] of integer; end;\n"
                        "  a3 = record a: array [1..3] of integer; end;\n"
                        "var\n"
                        "  r: a2;\n"
                        "  s: a3;\n"
                        "begin\n"
                        "  r.a := s.a;\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("mismatched record array-field bounds should diagnose");
        }
    }

    // Whole-record assign rejects records that differ only in an array field's bounds.
    {
        ScanAnalyse run("recshapebad.pas",
                        "program P;\n"
                        "type\n"
                        "  a2 = record a: array [1..2] of integer; end;\n"
                        "  a3 = record a: array [1..3] of integer; end;\n"
                        "var\n"
                        "  p: a2;\n"
                        "  q: a3;\n"
                        "begin\n"
                        "  q := p;\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("whole-record assign with mismatched array-field bounds should diagnose");
        }
    }

    // Duplicate record field names are diagnosed.
    {
        ScanAnalyse run("recdup.pas",
                        "program P;\n"
                        "type\n"
                        "  bad = record x: integer; x: real; end;\n"
                        "var\n"
                        "  r: bad;\n"
                        "begin\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("duplicate record field should diagnose");
        }
    }

    // Nested record fields are rejected.
    {
        ScanAnalyse run("recordnest.pas",
                        "program P;\n"
                        "type\n"
                        "  inner = record x: integer end;\n"
                        "  outer = record n: inner end;\n"
                        "var\n"
                        "  o: outer;\n"
                        "begin\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("nested record field type should diagnose");
        }
    }

    // Array function results are not a simple type.
    {
        ScanAnalyse run("arrayresult.pas",
                        "program P;\n"
                        "type\n"
                        "  t = array [1..3] of integer;\n"
                        "function f: t;\n"
                        "begin\n"
                        "end;\n"
                        "begin\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("array function result should diagnose");
        }
    }

    // Real literal outside double range.
    {
        ScanAnalyse run("realbig.pas",
                        "program P;\n"
                        "var\n"
                        "  x: real;\n"
                        "begin\n"
                        "  x := 1e999;\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("out-of-range real literal should diagnose");
        }
    }

    // Representable real literals stay clean, including as const initializers.
    {
        ScanAnalyse run("realok.pas",
                        "program P;\n"
                        "const\n"
                        "  pi = 3.14159265358979;\n"
                        "var\n"
                        "  x: real;\n"
                        "begin\n"
                        "  x := pi;\n"
                        "  x := 1e-300;\n"
                        "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("representable real literals should be clean");
        }
    }

    // Integer case with labels, range, and else.
    {
        ScanAnalyse run("caseok.pas",
                        "program P;\n"
                        "var n: integer;\n"
                        "begin\n"
                        "  case n of\n"
                        "    1, 2: n := 1;\n"
                        "    3..5: n := 2;\n"
                        "  else n := 0\n"
                        "  end\n"
                        "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("integer case should be clean");
        }
    }

    // Char and boolean case selectors.
    {
        ScanAnalyse run("casechar.pas",
                        "program P;\n"
                        "var c: char; b: boolean; n: integer;\n"
                        "begin\n"
                        "  case c of\n"
                        "    'a'..'c': n := 1;\n"
                        "    'z': n := 2\n"
                        "  end;\n"
                        "  case b of\n"
                        "    true: n := 1;\n"
                        "    false: n := 0\n"
                        "  end\n"
                        "end.\n");
        if (run.diagnostics.errorCount() != 0) {
            return fail("char/boolean case should be clean");
        }
    }

    // Non-ordinal selector and overlapping labels.
    {
        ScanAnalyse run("casebad.pas",
                        "program P;\n"
                        "var x: real; n: integer;\n"
                        "begin\n"
                        "  case x of 1: n := 1 end\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("real case selector should diagnose");
        }
        ScanAnalyse overlap("caseoverlap.pas",
                            "program P;\n"
                            "var n: integer;\n"
                            "begin\n"
                            "  case n of\n"
                            "    1..3: n := 1;\n"
                            "    2: n := 2\n"
                            "  end\n"
                            "end.\n");
        if (overlap.diagnostics.errorCount() == 0) {
            return fail("overlapping case labels should diagnose");
        }
    }

    // Enclosing-scope locals diagnosed at analyse time.
    {
        ScanAnalyse run("outerscope.pas",
                        "program HasOuterAccess;\n"
                        "var total: integer;\n"
                        "procedure Bad;\n"
                        "begin\n"
                        "  total := total + 1;\n"
                        "end;\n"
                        "begin\n"
                        "  Bad;\n"
                        "end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("enclosing-scope access should diagnose at analyse");
        }
    }

    // Nested subprograms diagnosed at analyse time.
    {
        ScanAnalyse run("nested.pas",
                        "program P;\n"
                        "procedure Outer;\n"
                        "  procedure Inner;\n"
                        "  begin end;\n"
                        "begin end;\n"
                        "begin end.\n");
        if (run.diagnostics.errorCount() == 0) {
            return fail("nested subprogram should diagnose at analyse");
        }
    }

    return 0;
}
