#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/TokenKind.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const char *message) {
    std::cerr << "pascal_scanner_test: " << message << '\n';
    return 1;
}

using apollo::pascal::TokenKind;

bool kindsEqual(const apollo::pascal::TokenStream &stream, const std::vector<TokenKind> &expected) {
    if (stream.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (stream[i].kind != expected[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    // Well-formed hello fixture
    {
        const auto source = apollo::common::SourceFile::fromString(
            "scan_hello.pas", "program Hello;\nbegin\n  writeln('Hello, Gemini!');\nend.\n");
        apollo::common::DiagnosticEngine diagnostics(source);
        const auto stream = apollo::pascal::scan(source, diagnostics);
        if (diagnostics.errorCount() != 0) {
            return fail("hello fixture should scan with no errors");
        }
        const std::vector<TokenKind> expected = {
            TokenKind::KeywordProgram, TokenKind::Identifier,   TokenKind::Semicolon,
            TokenKind::KeywordBegin,   TokenKind::Identifier,   TokenKind::LeftParen,
            TokenKind::StringLiteral,  TokenKind::RightParen,   TokenKind::Semicolon,
            TokenKind::KeywordEnd,     TokenKind::Dot,          TokenKind::EndOfFile,
        };
        if (!kindsEqual(stream, expected)) {
            return fail("hello fixture token kinds mismatch");
        }
        if (stream[6].lexeme != "'Hello, Gemini!'") {
            return fail("string lexeme should include quotes");
        }
    }

    // Keyword case folding
    {
        const auto source = apollo::common::SourceFile::fromString("kw.pas", "Begin BEGIN Beginner");
        apollo::common::DiagnosticEngine diagnostics(source);
        const auto stream = apollo::pascal::scan(source, diagnostics);
        if (stream.size() != 4 || stream[0].kind != TokenKind::KeywordBegin ||
            stream[1].kind != TokenKind::KeywordBegin || stream[2].kind != TokenKind::Identifier ||
            stream[3].kind != TokenKind::EndOfFile) {
            return fail("keyword case folding failed");
        }
    }

    // Comments produce no tokens
    {
        const auto source =
            apollo::common::SourceFile::fromString("c.pas", "a { hide } b (* hide *) c");
        apollo::common::DiagnosticEngine diagnostics(source);
        const auto stream = apollo::pascal::scan(source, diagnostics);
        if (diagnostics.errorCount() != 0) {
            return fail("comments should not error");
        }
        if (stream.size() != 4 || stream[0].kind != TokenKind::Identifier ||
            stream[1].kind != TokenKind::Identifier || stream[2].kind != TokenKind::Identifier ||
            stream[3].kind != TokenKind::EndOfFile) {
            return fail("comments should leave only identifiers");
        }
    }

    // 1..2 and real
    {
        const auto source = apollo::common::SourceFile::fromString("n.pas", "1..2 3.14");
        apollo::common::DiagnosticEngine diagnostics(source);
        const auto stream = apollo::pascal::scan(source, diagnostics);
        if (stream.size() != 5 || stream[0].kind != TokenKind::IntegerLiteral ||
            stream[1].kind != TokenKind::DotDot || stream[2].kind != TokenKind::IntegerLiteral ||
            stream[3].kind != TokenKind::RealLiteral || stream[4].kind != TokenKind::EndOfFile) {
            return fail("number / range scanning failed");
        }
    }

    // Char vs string; embedded quote
    {
        const auto source =
            apollo::common::SourceFile::fromString("s.pas", "'x' 'hi' 'it''s'");
        apollo::common::DiagnosticEngine diagnostics(source);
        const auto stream = apollo::pascal::scan(source, diagnostics);
        if (diagnostics.errorCount() != 0) {
            return fail("quoted literals should not error");
        }
        if (stream.size() != 4 || stream[0].kind != TokenKind::CharLiteral ||
            stream[1].kind != TokenKind::StringLiteral || stream[2].kind != TokenKind::StringLiteral ||
            stream[3].kind != TokenKind::EndOfFile) {
            return fail("char/string classification failed");
        }
    }

    // Unclosed comment recovery
    {
        const auto source = apollo::common::SourceFile::fromString("bad.pas", "a { unclosed");
        apollo::common::DiagnosticEngine diagnostics(source);
        const auto stream = apollo::pascal::scan(source, diagnostics);
        if (diagnostics.errorCount() < 1) {
            return fail("unclosed comment should report an error");
        }
        if (stream.empty() || stream[stream.size() - 1].kind != TokenKind::EndOfFile) {
            return fail("stream must end with EndOfFile after unclosed comment");
        }
    }

    // Unclosed string recovery
    {
        const auto source = apollo::common::SourceFile::fromString("bad2.pas", "x := 'oops\ny");
        apollo::common::DiagnosticEngine diagnostics(source);
        const auto stream = apollo::pascal::scan(source, diagnostics);
        if (diagnostics.errorCount() < 1) {
            return fail("unclosed string should report an error");
        }
        if (stream.empty() || stream[stream.size() - 1].kind != TokenKind::EndOfFile) {
            return fail("stream must end with EndOfFile after unclosed string");
        }
        bool sawIdentY = false;
        for (const auto &token : stream) {
            if (token.kind == TokenKind::Identifier && token.lexeme == "y") {
                sawIdentY = true;
            }
        }
        if (!sawIdentY) {
            return fail("should recover and scan identifier after unclosed string");
        }
    }

    std::cout << "pascal_scanner_test: ok\n";
    return 0;
}
