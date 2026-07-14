#include "apollo/pascal/Parser.hpp"

#include "apollo/pascal/TokenCursor.hpp"
#include "apollo/pascal/TokenKind.hpp"

#include <optional>
#include <utility>

namespace apollo::pascal {
namespace {

apollo::common::SourceRange span(const Token &beginTok, const Token &endTok) {
    return apollo::common::SourceRange{beginTok.range.begin, endTok.range.end};
}

apollo::common::SourceRange spanRanges(const apollo::common::SourceRange &begin,
                                       const apollo::common::SourceRange &end) {
    return apollo::common::SourceRange{begin.begin, end.end};
}

void skipUntil(TokenCursor &cursor, TokenKind a, TokenKind b) {
    while (!cursor.check(TokenKind::EndOfFile) && !cursor.check(a) && !cursor.check(b)) {
        cursor.advance();
    }
}

void syncStatement(TokenCursor &cursor) {
    skipUntil(cursor, TokenKind::Semicolon, TokenKind::KeywordEnd);
}

std::optional<ast::BinaryOp> multiplicativeOp(TokenKind kind) {
    switch (kind) {
    case TokenKind::Star:
        return ast::BinaryOp::Star;
    case TokenKind::Slash:
        return ast::BinaryOp::Slash;
    case TokenKind::KeywordDiv:
        return ast::BinaryOp::Div;
    case TokenKind::KeywordMod:
        return ast::BinaryOp::Mod;
    case TokenKind::KeywordAnd:
        return ast::BinaryOp::And;
    default:
        return std::nullopt;
    }
}

std::optional<ast::BinaryOp> additiveOp(TokenKind kind) {
    switch (kind) {
    case TokenKind::Plus:
        return ast::BinaryOp::Plus;
    case TokenKind::Minus:
        return ast::BinaryOp::Minus;
    case TokenKind::KeywordOr:
        return ast::BinaryOp::Or;
    default:
        return std::nullopt;
    }
}

std::optional<ast::BinaryOp> relationalOp(TokenKind kind) {
    switch (kind) {
    case TokenKind::Equal:
        return ast::BinaryOp::Equal;
    case TokenKind::NotEqual:
        return ast::BinaryOp::NotEqual;
    case TokenKind::Less:
        return ast::BinaryOp::Less;
    case TokenKind::LessEqual:
        return ast::BinaryOp::LessEqual;
    case TokenKind::Greater:
        return ast::BinaryOp::Greater;
    case TokenKind::GreaterEqual:
        return ast::BinaryOp::GreaterEqual;
    default:
        return std::nullopt;
    }
}

std::unique_ptr<ast::Expr> parseExpression(TokenCursor &cursor);
std::unique_ptr<ast::Expr> parseFactor(TokenCursor &cursor);

std::vector<std::unique_ptr<ast::Expr>> parseArgList(TokenCursor &cursor) {
    std::vector<std::unique_ptr<ast::Expr>> args;
    if (cursor.check(TokenKind::RightParen)) {
        return args;
    }
    for (;;) {
        auto arg = parseExpression(cursor);
        if (!arg) {
            syncStatement(cursor);
            break;
        }
        args.push_back(std::move(arg));
        if (!cursor.match(TokenKind::Comma)) {
            break;
        }
    }
    return args;
}

std::unique_ptr<ast::Expr> parseFactor(TokenCursor &cursor) {
    const Token &tok = cursor.current();

    if (cursor.match(TokenKind::KeywordNot)) {
        auto operand = parseFactor(cursor);
        auto expr = std::make_unique<ast::Expr>();
        expr->kind = ast::ExprKind::Unary;
        expr->unaryOp = ast::UnaryOp::Not;
        expr->left = std::move(operand);
        if (expr->left) {
            expr->range = spanRanges(tok.range, expr->left->range);
        } else {
            expr->range = tok.range;
        }
        return expr;
    }

    if (cursor.check(TokenKind::Plus) || cursor.check(TokenKind::Minus)) {
        const auto op =
            cursor.check(TokenKind::Plus) ? ast::UnaryOp::Plus : ast::UnaryOp::Minus;
        const Token &opTok = cursor.current();
        cursor.advance();
        auto operand = parseFactor(cursor);
        auto expr = std::make_unique<ast::Expr>();
        expr->kind = ast::ExprKind::Unary;
        expr->unaryOp = op;
        expr->left = std::move(operand);
        if (expr->left) {
            expr->range = spanRanges(opTok.range, expr->left->range);
        } else {
            expr->range = opTok.range;
        }
        return expr;
    }

    if (cursor.match(TokenKind::LeftParen)) {
        auto inner = parseExpression(cursor);
        const Token &closeTok = cursor.current();
        (void)cursor.expect(TokenKind::RightParen, "expected ')'");
        auto expr = std::make_unique<ast::Expr>();
        expr->kind = ast::ExprKind::Group;
        expr->left = std::move(inner);
        expr->range = span(tok, closeTok);
        return expr;
    }

    if (cursor.check(TokenKind::IntegerLiteral) || cursor.check(TokenKind::RealLiteral) ||
        cursor.check(TokenKind::StringLiteral) || cursor.check(TokenKind::CharLiteral)) {
        auto expr = std::make_unique<ast::Expr>();
        switch (cursor.current().kind) {
        case TokenKind::IntegerLiteral:
            expr->kind = ast::ExprKind::IntegerLiteral;
            break;
        case TokenKind::RealLiteral:
            expr->kind = ast::ExprKind::RealLiteral;
            break;
        case TokenKind::StringLiteral:
            expr->kind = ast::ExprKind::StringLiteral;
            break;
        default:
            expr->kind = ast::ExprKind::CharLiteral;
            break;
        }
        expr->text = std::string(cursor.current().lexeme);
        expr->range = cursor.current().range;
        cursor.advance();
        return expr;
    }

    if (cursor.check(TokenKind::Identifier)) {
        auto expr = std::make_unique<ast::Expr>();
        expr->text = std::string(cursor.current().lexeme);
        expr->range = cursor.current().range;
        cursor.advance();
        if (cursor.match(TokenKind::LeftParen)) {
            expr->kind = ast::ExprKind::Call;
            expr->args = parseArgList(cursor);
            const Token &closeTok = cursor.current();
            (void)cursor.expect(TokenKind::RightParen, "expected ')' after arguments");
            expr->range = spanRanges(expr->range, closeTok.range);
            return expr;
        }
        expr->kind = ast::ExprKind::Identifier;
        return expr;
    }

    (void)cursor.expect(TokenKind::Identifier, "expected expression");
    return nullptr;
}

std::unique_ptr<ast::Expr> parseTerm(TokenCursor &cursor) {
    auto left = parseFactor(cursor);
    if (!left) {
        return nullptr;
    }
    while (auto op = multiplicativeOp(cursor.current().kind)) {
        cursor.advance();
        auto right = parseFactor(cursor);
        auto expr = std::make_unique<ast::Expr>();
        expr->kind = ast::ExprKind::Binary;
        expr->binaryOp = *op;
        expr->left = std::move(left);
        expr->right = std::move(right);
        if (expr->right) {
            expr->range = spanRanges(expr->left->range, expr->right->range);
        } else {
            expr->range = expr->left->range;
        }
        left = std::move(expr);
    }
    return left;
}

std::unique_ptr<ast::Expr> parseSimpleExpression(TokenCursor &cursor) {
    auto left = parseTerm(cursor);
    if (!left) {
        return nullptr;
    }
    while (auto op = additiveOp(cursor.current().kind)) {
        cursor.advance();
        auto right = parseTerm(cursor);
        auto expr = std::make_unique<ast::Expr>();
        expr->kind = ast::ExprKind::Binary;
        expr->binaryOp = *op;
        expr->left = std::move(left);
        expr->right = std::move(right);
        if (expr->right) {
            expr->range = spanRanges(expr->left->range, expr->right->range);
        } else {
            expr->range = expr->left->range;
        }
        left = std::move(expr);
    }
    return left;
}

std::unique_ptr<ast::Expr> parseExpression(TokenCursor &cursor) {
    auto left = parseSimpleExpression(cursor);
    if (!left) {
        return nullptr;
    }
    if (auto op = relationalOp(cursor.current().kind)) {
        cursor.advance();
        auto right = parseSimpleExpression(cursor);
        auto expr = std::make_unique<ast::Expr>();
        expr->kind = ast::ExprKind::Binary;
        expr->binaryOp = *op;
        expr->left = std::move(left);
        expr->right = std::move(right);
        if (expr->right) {
            expr->range = spanRanges(expr->left->range, expr->right->range);
        } else {
            expr->range = expr->left->range;
        }
        return expr;
    }
    return left;
}

ast::Stmt parseStatement(TokenCursor &cursor);

ast::CompoundStmt parseCompoundStmt(TokenCursor &cursor) {
    ast::CompoundStmt compound;
    const Token &beginTok = cursor.current();
    if (!cursor.expect(TokenKind::KeywordBegin, "expected 'begin'")) {
        compound.range = beginTok.range;
        return compound;
    }

    if (!cursor.check(TokenKind::KeywordEnd)) {
        compound.statements.push_back(parseStatement(cursor));
        while (cursor.match(TokenKind::Semicolon)) {
            if (cursor.check(TokenKind::KeywordEnd) || cursor.check(TokenKind::EndOfFile) ||
                cursor.check(TokenKind::Dot)) {
                break;
            }
            compound.statements.push_back(parseStatement(cursor));
        }
    }

    const Token &endTok = cursor.current();
    if (!cursor.expect(TokenKind::KeywordEnd, "expected 'end'")) {
        compound.range = span(beginTok, endTok);
        return compound;
    }

    compound.range = span(beginTok, endTok);
    return compound;
}

ast::Stmt parseStatement(TokenCursor &cursor) {
    ast::Stmt stmt;

    if (cursor.check(TokenKind::KeywordBegin)) {
        auto compound = parseCompoundStmt(cursor);
        stmt.kind = ast::StmtKind::Compound;
        stmt.range = compound.range;
        stmt.statements = std::move(compound.statements);
        return stmt;
    }

    if (cursor.check(TokenKind::Identifier)) {
        const Token &nameTok = cursor.current();
        stmt.name = std::string(nameTok.lexeme);
        stmt.range = nameTok.range;
        cursor.advance();

        if (cursor.match(TokenKind::Assign)) {
            stmt.kind = ast::StmtKind::Assign;
            stmt.value = parseExpression(cursor);
            if (!stmt.value) {
                syncStatement(cursor);
            } else {
                stmt.range = spanRanges(nameTok.range, stmt.value->range);
            }
            return stmt;
        }

        stmt.kind = ast::StmtKind::Call;
        if (cursor.match(TokenKind::LeftParen)) {
            stmt.args = parseArgList(cursor);
            const Token &closeTok = cursor.current();
            (void)cursor.expect(TokenKind::RightParen, "expected ')' after arguments");
            stmt.range = spanRanges(nameTok.range, closeTok.range);
        }
        return stmt;
    }

    (void)cursor.expect(TokenKind::Identifier, "expected statement");
    syncStatement(cursor);
    stmt.kind = ast::StmtKind::Call;
    stmt.range = cursor.current().range;
    return stmt;
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

    if (!cursor.check(TokenKind::EndOfFile)) {
        (void)cursor.expect(TokenKind::EndOfFile, "expected end of file after '.'");
    }

    unit->range = span(programTok, dotTok);
    return unit;
}

} // namespace apollo::pascal
