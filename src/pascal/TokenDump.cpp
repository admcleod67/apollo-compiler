#include "apollo/pascal/TokenDump.hpp"

#include <ostream>

namespace apollo::pascal {

const char *tokenKindName(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::EndOfFile:
        return "EndOfFile";
    case TokenKind::KeywordAnd:
        return "KeywordAnd";
    case TokenKind::KeywordArray:
        return "KeywordArray";
    case TokenKind::KeywordBegin:
        return "KeywordBegin";
    case TokenKind::KeywordCase:
        return "KeywordCase";
    case TokenKind::KeywordConst:
        return "KeywordConst";
    case TokenKind::KeywordDiv:
        return "KeywordDiv";
    case TokenKind::KeywordDo:
        return "KeywordDo";
    case TokenKind::KeywordDownto:
        return "KeywordDownto";
    case TokenKind::KeywordElse:
        return "KeywordElse";
    case TokenKind::KeywordEnd:
        return "KeywordEnd";
    case TokenKind::KeywordFile:
        return "KeywordFile";
    case TokenKind::KeywordFor:
        return "KeywordFor";
    case TokenKind::KeywordFunction:
        return "KeywordFunction";
    case TokenKind::KeywordGoto:
        return "KeywordGoto";
    case TokenKind::KeywordIf:
        return "KeywordIf";
    case TokenKind::KeywordIn:
        return "KeywordIn";
    case TokenKind::KeywordLabel:
        return "KeywordLabel";
    case TokenKind::KeywordMod:
        return "KeywordMod";
    case TokenKind::KeywordNil:
        return "KeywordNil";
    case TokenKind::KeywordNot:
        return "KeywordNot";
    case TokenKind::KeywordOf:
        return "KeywordOf";
    case TokenKind::KeywordOr:
        return "KeywordOr";
    case TokenKind::KeywordPacked:
        return "KeywordPacked";
    case TokenKind::KeywordProcedure:
        return "KeywordProcedure";
    case TokenKind::KeywordProgram:
        return "KeywordProgram";
    case TokenKind::KeywordRecord:
        return "KeywordRecord";
    case TokenKind::KeywordRepeat:
        return "KeywordRepeat";
    case TokenKind::KeywordSet:
        return "KeywordSet";
    case TokenKind::KeywordThen:
        return "KeywordThen";
    case TokenKind::KeywordTo:
        return "KeywordTo";
    case TokenKind::KeywordType:
        return "KeywordType";
    case TokenKind::KeywordUntil:
        return "KeywordUntil";
    case TokenKind::KeywordVar:
        return "KeywordVar";
    case TokenKind::KeywordWhile:
        return "KeywordWhile";
    case TokenKind::KeywordWith:
        return "KeywordWith";
    case TokenKind::Identifier:
        return "Identifier";
    case TokenKind::IntegerLiteral:
        return "IntegerLiteral";
    case TokenKind::RealLiteral:
        return "RealLiteral";
    case TokenKind::StringLiteral:
        return "StringLiteral";
    case TokenKind::CharLiteral:
        return "CharLiteral";
    case TokenKind::Plus:
        return "Plus";
    case TokenKind::Minus:
        return "Minus";
    case TokenKind::Star:
        return "Star";
    case TokenKind::Slash:
        return "Slash";
    case TokenKind::Equal:
        return "Equal";
    case TokenKind::Less:
        return "Less";
    case TokenKind::Greater:
        return "Greater";
    case TokenKind::LeftBracket:
        return "LeftBracket";
    case TokenKind::RightBracket:
        return "RightBracket";
    case TokenKind::Dot:
        return "Dot";
    case TokenKind::Comma:
        return "Comma";
    case TokenKind::Colon:
        return "Colon";
    case TokenKind::Semicolon:
        return "Semicolon";
    case TokenKind::LeftParen:
        return "LeftParen";
    case TokenKind::RightParen:
        return "RightParen";
    case TokenKind::Caret:
        return "Caret";
    case TokenKind::At:
        return "At";
    case TokenKind::Assign:
        return "Assign";
    case TokenKind::NotEqual:
        return "NotEqual";
    case TokenKind::LessEqual:
        return "LessEqual";
    case TokenKind::GreaterEqual:
        return "GreaterEqual";
    case TokenKind::DotDot:
        return "DotDot";
    }
    return "Unknown";
}

void writeTokenDump(std::ostream &out, const TokenStream &stream) {
    for (const auto &token : stream) {
        const auto &begin = token.range.begin;
        const auto &end = token.range.end;
        out << begin.line << ':' << begin.column << '-' << end.line << ':' << end.column << "  "
            << tokenKindName(token.kind) << "  " << token.lexeme << '\n';
    }
}

} // namespace apollo::pascal
