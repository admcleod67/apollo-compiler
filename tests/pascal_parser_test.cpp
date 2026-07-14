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

    return 0;
}
