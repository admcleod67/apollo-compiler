#include "apollo/pascal/Parser.hpp"

#include "apollo/pascal/TokenCursor.hpp"
#include "apollo/pascal/TokenKind.hpp"

namespace apollo::pascal {
namespace {

apollo::common::SourceRange span(const Token &beginTok, const Token &endTok) {
    return apollo::common::SourceRange{beginTok.range.begin, endTok.range.end};
}

void skipUntil(TokenCursor &cursor, TokenKind a, TokenKind b) {
    while (!cursor.check(TokenKind::EndOfFile) && !cursor.check(a) && !cursor.check(b)) {
        cursor.advance();
    }
}

ast::CompoundStmt parseCompoundStmt(TokenCursor &cursor) {
    ast::CompoundStmt compound;
    const Token &beginTok = cursor.current();
    if (!cursor.expect(TokenKind::KeywordBegin, "expected 'begin'")) {
        compound.range = beginTok.range;
        return compound;
    }

    // Stage 1: no statements between begin and end.
    if (!cursor.check(TokenKind::KeywordEnd)) {
        (void)cursor.expect(TokenKind::KeywordEnd,
                            "expected 'end' (statements are not supported yet)");
        skipUntil(cursor, TokenKind::KeywordEnd, TokenKind::Dot);
    }

    const Token &endTok = cursor.current();
    if (!cursor.expect(TokenKind::KeywordEnd, "expected 'end'")) {
        compound.range = span(beginTok, endTok);
        return compound;
    }

    compound.range = span(beginTok, endTok);
    return compound;
}

ast::Block parseBlock(TokenCursor &cursor) {
    ast::Block block;
    block.body = parseCompoundStmt(cursor);
    block.range = block.body.range;
    return block;
}

} // namespace

std::unique_ptr<ast::Program> parse(const apollo::common::SourceFile & /*source*/,
                                    const TokenStream &tokens,
                                    apollo::common::DiagnosticEngine &diagnostics) {
    TokenCursor cursor(tokens, diagnostics);

    const Token &programTok = cursor.current();
    if (!cursor.expect(TokenKind::KeywordProgram, "expected 'program'")) {
        return nullptr;
    }

    auto unit = std::make_unique<ast::Program>();

    if (cursor.check(TokenKind::Identifier)) {
        unit->name = std::string(cursor.current().lexeme);
        cursor.advance();
    } else {
        (void)cursor.expect(TokenKind::Identifier, "expected program name");
    }

    (void)cursor.expect(TokenKind::Semicolon, "expected ';' after program name");

    unit->block = parseBlock(cursor);

    const Token &dotTok = cursor.current();
    (void)cursor.expect(TokenKind::Dot, "expected '.' after program block");

    // Allow being at EOF; complain if extra tokens remain before EOF.
    if (!cursor.check(TokenKind::EndOfFile)) {
        (void)cursor.expect(TokenKind::EndOfFile, "expected end of file after '.'");
    }

    unit->range = span(programTok, dotTok);
    return unit;
}

} // namespace apollo::pascal
