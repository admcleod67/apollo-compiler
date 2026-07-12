//
// Pascal token kind enumerators for the Apollo scanner.
//

#ifndef APOLLO_PASCAL_TOKEN_KIND_HPP
#define APOLLO_PASCAL_TOKEN_KIND_HPP

#pragma once

namespace apollo::pascal {

enum class TokenKind {
    EndOfFile,

    // Keywords
    KeywordAnd,
    KeywordArray,
    KeywordBegin,
    KeywordCase,
    KeywordConst,
    KeywordDiv,
    KeywordDo,
    KeywordDownto,
    KeywordElse,
    KeywordEnd,
    KeywordFile,
    KeywordFor,
    KeywordFunction,
    KeywordGoto,
    KeywordIf,
    KeywordIn,
    KeywordLabel,
    KeywordMod,
    KeywordNil,
    KeywordNot,
    KeywordOf,
    KeywordOr,
    KeywordPacked,
    KeywordProcedure,
    KeywordProgram,
    KeywordRecord,
    KeywordRepeat,
    KeywordSet,
    KeywordThen,
    KeywordTo,
    KeywordType,
    KeywordUntil,
    KeywordVar,
    KeywordWhile,
    KeywordWith,

    Identifier,
    IntegerLiteral,
    RealLiteral,
    StringLiteral,
    CharLiteral,

    Plus,
    Minus,
    Star,
    Slash,
    Equal,
    Less,
    Greater,
    LeftBracket,
    RightBracket,
    Dot,
    Comma,
    Colon,
    Semicolon,
    LeftParen,
    RightParen,
    Caret,
    At,
    Assign,
    NotEqual,
    LessEqual,
    GreaterEqual,
    DotDot,
};

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_TOKEN_KIND_HPP
