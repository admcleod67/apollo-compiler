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

void syncDeclaration(TokenCursor &cursor) {
    while (!cursor.check(TokenKind::EndOfFile) && !cursor.check(TokenKind::Semicolon) &&
           !cursor.check(TokenKind::KeywordBegin) && !cursor.check(TokenKind::KeywordConst) &&
           !cursor.check(TokenKind::KeywordType) && !cursor.check(TokenKind::KeywordVar) &&
           !cursor.check(TokenKind::KeywordProcedure) &&
           !cursor.check(TokenKind::KeywordFunction)) {
        cursor.advance();
    }
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
ast::TypeDenoter parseTypeDenoter(TokenCursor &cursor);
ast::Block parseBlock(TokenCursor &cursor);
ast::Stmt parseStatement(TokenCursor &cursor);

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

ast::TypeDenoter parseTypeDenoter(TokenCursor &cursor) {
    ast::TypeDenoter type;
    const Token &start = cursor.current();

    if (cursor.check(TokenKind::KeywordFile)) {
        cursor.error("file types are not supported yet");
        type.kind = ast::TypeKind::Named;
        type.range = start.range;
        syncDeclaration(cursor);
        return type;
    }

    if (cursor.match(TokenKind::KeywordArray)) {
        type.kind = ast::TypeKind::Array;
        (void)cursor.expect(TokenKind::LeftBracket, "expected '[' after 'array'");
        type.indexLow = parseExpression(cursor);
        (void)cursor.expect(TokenKind::DotDot, "expected '..' in array index range");
        type.indexHigh = parseExpression(cursor);
        (void)cursor.expect(TokenKind::RightBracket, "expected ']' after array index range");
        (void)cursor.expect(TokenKind::KeywordOf, "expected 'of' after array index range");
        type.element = std::make_unique<ast::TypeDenoter>(parseTypeDenoter(cursor));
        if (type.element) {
            type.range = spanRanges(start.range, type.element->range);
        } else {
            type.range = start.range;
        }
        return type;
    }

    if (cursor.check(TokenKind::Identifier)) {
        type.kind = ast::TypeKind::Named;
        type.name = std::string(cursor.current().lexeme);
        type.range = cursor.current().range;
        cursor.advance();
        return type;
    }

    cursor.error("expected type");
    type.kind = ast::TypeKind::Named;
    type.range = start.range;
    syncDeclaration(cursor);
    return type;
}

std::vector<std::string> parseIdentList(TokenCursor &cursor) {
    std::vector<std::string> names;
    if (!cursor.check(TokenKind::Identifier)) {
        (void)cursor.expect(TokenKind::Identifier, "expected identifier");
        return names;
    }
    names.push_back(std::string(cursor.current().lexeme));
    cursor.advance();
    while (cursor.match(TokenKind::Comma)) {
        if (!cursor.check(TokenKind::Identifier)) {
            (void)cursor.expect(TokenKind::Identifier, "expected identifier");
            break;
        }
        names.push_back(std::string(cursor.current().lexeme));
        cursor.advance();
    }
    return names;
}

void parseConstSection(TokenCursor &cursor, ast::Block &block) {
    cursor.advance(); // const
    while (cursor.check(TokenKind::Identifier)) {
        ast::ConstDecl decl;
        const Token &nameTok = cursor.current();
        decl.name = std::string(nameTok.lexeme);
        cursor.advance();
        (void)cursor.expect(TokenKind::Equal, "expected '=' in const declaration");
        decl.value = parseExpression(cursor);
        const Token &semi = cursor.current();
        (void)cursor.expect(TokenKind::Semicolon, "expected ';' after const declaration");
        if (decl.value) {
            decl.range = spanRanges(nameTok.range, decl.value->range);
        } else {
            decl.range = nameTok.range;
            syncDeclaration(cursor);
        }
        (void)semi;
        block.consts.push_back(std::move(decl));
    }
}

void parseTypeSection(TokenCursor &cursor, ast::Block &block) {
    cursor.advance(); // type
    while (cursor.check(TokenKind::Identifier)) {
        ast::TypeDecl decl;
        const Token &nameTok = cursor.current();
        decl.name = std::string(nameTok.lexeme);
        cursor.advance();
        (void)cursor.expect(TokenKind::Equal, "expected '=' in type declaration");
        decl.type = parseTypeDenoter(cursor);
        (void)cursor.expect(TokenKind::Semicolon, "expected ';' after type declaration");
        decl.range = spanRanges(nameTok.range, decl.type.range);
        block.types.push_back(std::move(decl));
    }
}

void parseVarSection(TokenCursor &cursor, ast::Block &block) {
    cursor.advance(); // var
    while (cursor.check(TokenKind::Identifier)) {
        ast::VarDecl decl;
        const Token &first = cursor.current();
        decl.names = parseIdentList(cursor);
        (void)cursor.expect(TokenKind::Colon, "expected ':' in var declaration");
        decl.type = parseTypeDenoter(cursor);
        (void)cursor.expect(TokenKind::Semicolon, "expected ';' after var declaration");
        decl.range = spanRanges(first.range, decl.type.range);
        block.vars.push_back(std::move(decl));
    }
}

std::vector<ast::ParamDecl> parseParamList(TokenCursor &cursor) {
    std::vector<ast::ParamDecl> params;
    if (!cursor.match(TokenKind::LeftParen)) {
        return params;
    }
    if (cursor.check(TokenKind::RightParen)) {
        (void)cursor.expect(TokenKind::RightParen, "expected ')'");
        return params;
    }
    for (;;) {
        ast::ParamDecl param;
        const Token &start = cursor.current();
        param.isVar = cursor.match(TokenKind::KeywordVar);
        param.names = parseIdentList(cursor);
        (void)cursor.expect(TokenKind::Colon, "expected ':' in parameter list");
        param.type = parseTypeDenoter(cursor);
        param.range = spanRanges(start.range, param.type.range);
        params.push_back(std::move(param));
        if (!cursor.match(TokenKind::Semicolon)) {
            break;
        }
    }
    (void)cursor.expect(TokenKind::RightParen, "expected ')' after parameter list");
    return params;
}

ast::Subprogram parseSubprogram(TokenCursor &cursor) {
    ast::Subprogram sub;
    const Token &start = cursor.current();
    sub.isFunction = cursor.check(TokenKind::KeywordFunction);
    if (sub.isFunction) {
        cursor.advance();
    } else {
        (void)cursor.expect(TokenKind::KeywordProcedure, "expected 'procedure' or 'function'");
    }

    if (cursor.check(TokenKind::Identifier)) {
        sub.name = std::string(cursor.current().lexeme);
        cursor.advance();
    } else {
        (void)cursor.expect(TokenKind::Identifier, "expected subprogram name");
    }

    sub.params = parseParamList(cursor);

    if (sub.isFunction) {
        (void)cursor.expect(TokenKind::Colon, "expected ':' before function result type");
        sub.returnType = parseTypeDenoter(cursor);
    }

    (void)cursor.expect(TokenKind::Semicolon, "expected ';' after subprogram heading");
    sub.block = std::make_unique<ast::Block>(parseBlock(cursor));
    (void)cursor.expect(TokenKind::Semicolon, "expected ';' after subprogram");

    if (sub.block) {
        sub.range = spanRanges(start.range, sub.block->range);
    } else {
        sub.range = start.range;
    }
    return sub;
}

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
                cursor.check(TokenKind::Dot) || cursor.check(TokenKind::KeywordUntil)) {
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

    if (cursor.check(TokenKind::KeywordIf)) {
        const Token &ifTok = cursor.current();
        cursor.advance();
        stmt.kind = ast::StmtKind::If;
        stmt.condition = parseExpression(cursor);
        (void)cursor.expect(TokenKind::KeywordThen, "expected 'then'");
        stmt.thenBranch = std::make_unique<ast::Stmt>(parseStatement(cursor));
        if (cursor.match(TokenKind::KeywordElse)) {
            stmt.elseBranch = std::make_unique<ast::Stmt>(parseStatement(cursor));
        }
        apollo::common::SourceRange endRange = ifTok.range;
        if (stmt.elseBranch) {
            endRange = stmt.elseBranch->range;
        } else if (stmt.thenBranch) {
            endRange = stmt.thenBranch->range;
        } else if (stmt.condition) {
            endRange = stmt.condition->range;
        }
        stmt.range = spanRanges(ifTok.range, endRange);
        return stmt;
    }

    if (cursor.check(TokenKind::KeywordWhile)) {
        const Token &whileTok = cursor.current();
        cursor.advance();
        stmt.kind = ast::StmtKind::While;
        stmt.condition = parseExpression(cursor);
        (void)cursor.expect(TokenKind::KeywordDo, "expected 'do'");
        stmt.thenBranch = std::make_unique<ast::Stmt>(parseStatement(cursor));
        apollo::common::SourceRange endRange = whileTok.range;
        if (stmt.thenBranch) {
            endRange = stmt.thenBranch->range;
        } else if (stmt.condition) {
            endRange = stmt.condition->range;
        }
        stmt.range = spanRanges(whileTok.range, endRange);
        return stmt;
    }

    if (cursor.check(TokenKind::KeywordRepeat)) {
        const Token &repeatTok = cursor.current();
        cursor.advance();
        stmt.kind = ast::StmtKind::Repeat;

        if (!cursor.check(TokenKind::KeywordUntil)) {
            stmt.statements.push_back(parseStatement(cursor));
            while (cursor.match(TokenKind::Semicolon)) {
                if (cursor.check(TokenKind::KeywordUntil) || cursor.check(TokenKind::EndOfFile)) {
                    break;
                }
                stmt.statements.push_back(parseStatement(cursor));
            }
        }
        (void)cursor.expect(TokenKind::KeywordUntil, "expected 'until'");
        stmt.condition = parseExpression(cursor);
        if (stmt.condition) {
            stmt.range = spanRanges(repeatTok.range, stmt.condition->range);
        } else {
            stmt.range = repeatTok.range;
        }
        return stmt;
    }

    if (cursor.check(TokenKind::KeywordFor)) {
        const Token &forTok = cursor.current();
        cursor.advance();
        stmt.kind = ast::StmtKind::For;
        if (cursor.check(TokenKind::Identifier)) {
            stmt.name = std::string(cursor.current().lexeme);
            cursor.advance();
        } else {
            (void)cursor.expect(TokenKind::Identifier, "expected for-loop control variable");
        }
        (void)cursor.expect(TokenKind::Assign, "expected ':=' in for statement");
        stmt.value = parseExpression(cursor);
        if (cursor.match(TokenKind::KeywordDownto)) {
            stmt.forDownto = true;
        } else {
            (void)cursor.expect(TokenKind::KeywordTo, "expected 'to' or 'downto'");
        }
        stmt.forLimit = parseExpression(cursor);
        (void)cursor.expect(TokenKind::KeywordDo, "expected 'do'");
        stmt.thenBranch = std::make_unique<ast::Stmt>(parseStatement(cursor));
        apollo::common::SourceRange endRange = forTok.range;
        if (stmt.thenBranch) {
            endRange = stmt.thenBranch->range;
        } else if (stmt.forLimit) {
            endRange = stmt.forLimit->range;
        }
        stmt.range = spanRanges(forTok.range, endRange);
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
    const Token &start = cursor.current();

    if (cursor.check(TokenKind::KeywordConst)) {
        parseConstSection(cursor, block);
    }
    if (cursor.check(TokenKind::KeywordType)) {
        parseTypeSection(cursor, block);
    }
    if (cursor.check(TokenKind::KeywordVar)) {
        parseVarSection(cursor, block);
    }

    while (cursor.check(TokenKind::KeywordProcedure) ||
           cursor.check(TokenKind::KeywordFunction)) {
        block.subprograms.push_back(parseSubprogram(cursor));
    }

    block.body = parseCompoundStmt(cursor);
    block.range = block.body.range;
    if (start.kind != TokenKind::KeywordBegin) {
        block.range = spanRanges(start.range, block.body.range);
    }
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
