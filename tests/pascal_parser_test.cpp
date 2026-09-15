#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/ast/Ast.hpp"

#include <iostream>
#include <memory>
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

using apollo::pascal::ast::BinaryOp;
using apollo::pascal::ast::ExprKind;
using apollo::pascal::ast::StmtKind;
using apollo::pascal::ast::UnaryOp;

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
            return fail("empty compound should have no statements");
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

    // Precedence: 1+2*3 => +(1, *(2,3))
    {
        ScanParse run("prec.pas", "program P; begin x := 1 + 2 * 3; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("precedence fixture should parse");
        }
        const auto &stmts = run.program->block.body.statements;
        if (stmts.size() != 1 || stmts[0].kind != StmtKind::Assign || !stmts[0].value) {
            return fail("precedence fixture should be one assignment");
        }
        const auto &root = *stmts[0].value;
        if (root.kind != ExprKind::Binary || root.binaryOp != BinaryOp::Plus || !root.left ||
            !root.right) {
            return fail("1+2*3 root should be Plus");
        }
        if (root.left->kind != ExprKind::IntegerLiteral || root.left->text != "1") {
            return fail("Plus left should be integer 1");
        }
        if (root.right->kind != ExprKind::Binary || root.right->binaryOp != BinaryOp::Star) {
            return fail("Plus right should be Star");
        }
        if (!root.right->left || root.right->left->text != "2" || !root.right->right ||
            root.right->right->text != "3") {
            return fail("Star should be 2 * 3");
        }
    }

    // not a and b => (not a) and b
    {
        ScanParse run("notand.pas", "program P; begin x := not a and b; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("not/and fixture should parse");
        }
        const auto &val = *run.program->block.body.statements[0].value;
        if (val.kind != ExprKind::Binary || val.binaryOp != BinaryOp::And || !val.left ||
            !val.right) {
            return fail("not a and b should be And at root");
        }
        if (val.left->kind != ExprKind::Unary || val.left->unaryOp != UnaryOp::Not ||
            !val.left->left || val.left->left->text != "a") {
            return fail("And left should be Not a");
        }
        if (val.right->kind != ExprKind::Identifier || val.right->text != "b") {
            return fail("And right should be b");
        }
    }

    // Assignment
    {
        ScanParse run("assign.pas", "program P; begin x := 1; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("assignment fixture should parse");
        }
        const auto &stmt = run.program->block.body.statements[0];
        if (stmt.kind != StmtKind::Assign || stmt.name != "x" || !stmt.value ||
            stmt.value->kind != ExprKind::IntegerLiteral || stmt.value->text != "1") {
            return fail("assignment AST mismatch");
        }
    }

    // Call / hello.pas shape
    {
        ScanParse run("hello.pas", "program Hello;\nbegin\n  writeln('Hello, Gemini!');\nend.\n");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("hello.pas should parse");
        }
        const auto &stmt = run.program->block.body.statements[0];
        if (stmt.kind != StmtKind::Call || stmt.name != "writeln" || stmt.args.size() != 1) {
            return fail("writeln call AST mismatch");
        }
        if (stmt.args[0]->kind != ExprKind::StringLiteral ||
            stmt.args[0]->text != "'Hello, Gemini!'") {
            return fail("writeln string arg mismatch");
        }
    }

    // Nested compound
    {
        ScanParse run("nest.pas", "program P; begin begin end end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("nested compound should parse");
        }
        const auto &stmts = run.program->block.body.statements;
        if (stmts.size() != 1 || stmts[0].kind != StmtKind::Compound) {
            return fail("expected one nested compound statement");
        }
        if (!stmts[0].statements.empty()) {
            return fail("inner compound should be empty");
        }
    }

    // Malformed expression recovery
    {
        ScanParse run("badexpr.pas", "program P; begin x := 1 + ; end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("malformed expression should diagnose");
        }
        if (!run.program) {
            return fail("malformed expression should not prevent a Program root");
        }
    }

    // var + named type
    {
        ScanParse run("var.pas", "program P; var i: integer; begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("var fixture should parse");
        }
        if (run.program->block.vars.size() != 1 || run.program->block.vars[0].names.size() != 1 ||
            run.program->block.vars[0].names[0] != "i" ||
            run.program->block.vars[0].type.name != "integer") {
            return fail("var AST mismatch");
        }
    }

    // const
    {
        ScanParse run("const.pas", "program P; const N = 10; begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("const fixture should parse");
        }
        if (run.program->block.consts.size() != 1 || run.program->block.consts[0].name != "N" ||
            !run.program->block.consts[0].value ||
            run.program->block.consts[0].value->text != "10") {
            return fail("const AST mismatch");
        }
    }

    // type alias
    {
        ScanParse run("type.pas", "program P; type T = integer; begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("type fixture should parse");
        }
        if (run.program->block.types.size() != 1 || run.program->block.types[0].name != "T" ||
            run.program->block.types[0].type.name != "integer") {
            return fail("type AST mismatch");
        }
    }

    // array type
    {
        ScanParse run("array.pas", "program P; var a: array [1..10] of integer; begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("array fixture should parse");
        }
        const auto &type = run.program->block.vars[0].type;
        if (type.kind != apollo::pascal::ast::TypeKind::Array || !type.indexLow ||
            type.indexLow->text != "1" || !type.indexHigh || type.indexHigh->text != "10" ||
            !type.element || type.element->name != "integer") {
            return fail("array type AST mismatch");
        }
    }

    // if / else
    {
        ScanParse run("if.pas", "program P; begin if a then b else c; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("if fixture should parse");
        }
        const auto &s = run.program->block.body.statements[0];
        if (s.kind != StmtKind::If || !s.condition || !s.thenBranch || !s.elseBranch ||
            s.thenBranch->kind != StmtKind::Call || s.thenBranch->name != "b" ||
            s.elseBranch->name != "c") {
            return fail("if AST mismatch");
        }
    }

    // while
    {
        ScanParse run("while.pas", "program P; begin while a do b; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("while fixture should parse");
        }
        const auto &s = run.program->block.body.statements[0];
        if (s.kind != StmtKind::While || !s.thenBranch || s.thenBranch->name != "b") {
            return fail("while AST mismatch");
        }
    }

    // repeat
    {
        ScanParse run("repeat.pas", "program P; begin repeat a until b; end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("repeat fixture should parse");
        }
        const auto &s = run.program->block.body.statements[0];
        if (s.kind != StmtKind::Repeat || s.statements.size() != 1 || !s.condition ||
            s.condition->text != "b") {
            return fail("repeat AST mismatch");
        }
    }

    // for to / downto
    {
        ScanParse run("for.pas", "program P; begin for i := 1 to 10 do writeln(i); end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("for fixture should parse");
        }
        const auto &s = run.program->block.body.statements[0];
        if (s.kind != StmtKind::For || s.name != "i" || s.forDownto || !s.value ||
            s.value->text != "1" || !s.forLimit || s.forLimit->text != "10" || !s.thenBranch ||
            s.thenBranch->kind != StmtKind::Call || s.thenBranch->name != "writeln") {
            return fail("for AST mismatch");
        }

        ScanParse down("downto.pas", "program P; begin for i := 10 downto 1 do i; end.");
        if (down.diagnostics.errorCount() != 0 || !down.program ||
            !down.program->block.body.statements[0].forDownto) {
            return fail("downto fixture should set forDownto");
        }
    }

    // nested procedure
    {
        ScanParse run("proc.pas", "program P; procedure Q; begin end; begin end.");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("procedure fixture should parse");
        }
        if (run.program->block.subprograms.size() != 1 ||
            run.program->block.subprograms[0].isFunction ||
            run.program->block.subprograms[0].name != "Q" ||
            !run.program->block.subprograms[0].block) {
            return fail("procedure AST mismatch");
        }
    }

    // file type rejected
    {
        ScanParse run("file.pas", "program P; var f: file of integer; begin end.");
        if (run.diagnostics.errorCount() == 0) {
            return fail("file type should diagnose");
        }
        if (!run.program) {
            return fail("file type error should not prevent a Program root");
        }
    }

    // count.pas shape
    {
        ScanParse run("count.pas",
                      "program Count;\nvar\n  i: integer;\nbegin\n  for i := 1 to 10 do\n    "
                      "writeln(i);\nend.\n");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("count.pas should parse");
        }
        if (run.program->block.vars.size() != 1 ||
            run.program->block.body.statements[0].kind != StmtKind::For ||
            !run.program->block.body.statements[0].thenBranch ||
            run.program->block.body.statements[0].thenBranch->name != "writeln") {
            return fail("count.pas AST mismatch");
        }
    }

    // flat record type and field select
    {
        ScanParse run("rec.pas",
                      "program P;\n"
                      "type\n"
                      "  point = record x, y: integer; end;\n"
                      "var\n"
                      "  p: point;\n"
                      "begin\n"
                      "  p.x := 1;\n"
                      "end.\n");
        if (run.diagnostics.errorCount() != 0 || !run.program) {
            return fail("record fixture should parse");
        }
        if (run.program->block.types.size() != 1 ||
            run.program->block.types[0].type.kind != apollo::pascal::ast::TypeKind::Record) {
            return fail("record type denoter missing");
        }
        const auto &stmt = run.program->block.body.statements[0];
        if (stmt.kind != StmtKind::Assign || stmt.fieldName != "x" || stmt.name != "p") {
            return fail("field assign AST mismatch");
        }
    }

    return 0;
}
