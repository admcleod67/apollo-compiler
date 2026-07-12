#include "apollo/pascal/Scanner.hpp"

#include "apollo/common/Diagnostic.hpp"
#include "apollo/common/SourceLocation.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace apollo::pascal {
namespace {

using apollo::common::DiagnosticEngine;
using apollo::common::DiagnosticSeverity;
using apollo::common::SourceFile;
using apollo::common::SourceLocation;
using apollo::common::SourceRange;

bool isAsciiLetter(const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isIdentStart(const char c) {
    return isAsciiLetter(c) || c == '_';
}

bool isIdentContinue(const char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

bool isDigit(const char c) {
    return c >= '0' && c <= '9';
}

bool isWhitespace(const char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

char toLowerAscii(const char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

std::string foldAsciiLower(const std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(toLowerAscii(c));
    }
    return out;
}

const std::unordered_map<std::string, TokenKind> &keywordTable() {
    static const std::unordered_map<std::string, TokenKind> table = {
        {"and", TokenKind::KeywordAnd},
        {"array", TokenKind::KeywordArray},
        {"begin", TokenKind::KeywordBegin},
        {"case", TokenKind::KeywordCase},
        {"const", TokenKind::KeywordConst},
        {"div", TokenKind::KeywordDiv},
        {"do", TokenKind::KeywordDo},
        {"downto", TokenKind::KeywordDownto},
        {"else", TokenKind::KeywordElse},
        {"end", TokenKind::KeywordEnd},
        {"file", TokenKind::KeywordFile},
        {"for", TokenKind::KeywordFor},
        {"function", TokenKind::KeywordFunction},
        {"goto", TokenKind::KeywordGoto},
        {"if", TokenKind::KeywordIf},
        {"in", TokenKind::KeywordIn},
        {"label", TokenKind::KeywordLabel},
        {"mod", TokenKind::KeywordMod},
        {"nil", TokenKind::KeywordNil},
        {"not", TokenKind::KeywordNot},
        {"of", TokenKind::KeywordOf},
        {"or", TokenKind::KeywordOr},
        {"packed", TokenKind::KeywordPacked},
        {"procedure", TokenKind::KeywordProcedure},
        {"program", TokenKind::KeywordProgram},
        {"record", TokenKind::KeywordRecord},
        {"repeat", TokenKind::KeywordRepeat},
        {"set", TokenKind::KeywordSet},
        {"then", TokenKind::KeywordThen},
        {"to", TokenKind::KeywordTo},
        {"type", TokenKind::KeywordType},
        {"until", TokenKind::KeywordUntil},
        {"var", TokenKind::KeywordVar},
        {"while", TokenKind::KeywordWhile},
        {"with", TokenKind::KeywordWith},
    };
    return table;
}

TokenKind lookupKeyword(const std::string_view lexeme) {
    const auto folded = foldAsciiLower(lexeme);
    const auto &table = keywordTable();
    const auto it = table.find(folded);
    if (it == table.end()) {
        return TokenKind::Identifier;
    }
    return it->second;
}

class ScannerImpl {
public:
    ScannerImpl(const SourceFile &source, DiagnosticEngine &diagnostics)
        : source_(&source), text_(source.text()), diagnostics_(&diagnostics) {}

    TokenStream run() {
        TokenStream stream;
        while (!atEnd()) {
            skipWhitespaceAndComments();
            if (atEnd()) {
                break;
            }
            if (auto token = nextToken()) {
                stream.push_back(*token);
            }
        }
        stream.push_back(makeToken(TokenKind::EndOfFile, pos_, pos_));
        return stream;
    }

private:
    const SourceFile *source_;
    std::string_view text_;
    DiagnosticEngine *diagnostics_;
    std::size_t pos_{0};

    [[nodiscard]] bool atEnd() const noexcept { return pos_ >= text_.size(); }

    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept {
        const std::size_t i = pos_ + ahead;
        if (i >= text_.size()) {
            return '\0';
        }
        return text_[i];
    }

    char advance() {
        return text_[pos_++];
    }

    [[nodiscard]] SourceLocation loc(const std::size_t offset) const { return source_->locationAt(offset); }

    [[nodiscard]] SourceRange range(const std::size_t begin, const std::size_t end) const {
        return SourceRange{loc(begin), loc(end)};
    }

    [[nodiscard]] Token makeToken(const TokenKind kind, const std::size_t begin, const std::size_t end) const {
        return Token{kind, range(begin, end), text_.substr(begin, end - begin)};
    }

    void errorAt(const std::size_t offset, std::string message) const {
        diagnostics_->report(DiagnosticSeverity::Error, loc(offset), std::move(message));
    }

    void skipWhitespaceAndComments() {
        while (!atEnd()) {
            const char c = peek();
            if (isWhitespace(c)) {
                advance();
                continue;
            }
            if (c == '{') {
                skipBraceComment();
                continue;
            }
            if (c == '(' && peek(1) == '*') {
                skipParenStarComment();
                continue;
            }
            break;
        }
    }

    void skipBraceComment() {
        const std::size_t start = pos_;
        advance(); // '{'
        while (!atEnd()) {
            if (peek() == '}') {
                advance();
                return;
            }
            advance();
        }
        errorAt(start, "unclosed block comment");
    }

    void skipParenStarComment() {
        const std::size_t start = pos_;
        advance(); // '('
        advance(); // '*'
        while (!atEnd()) {
            if (peek() == '*' && peek(1) == ')') {
                advance();
                advance();
                return;
            }
            advance();
        }
        errorAt(start, "unclosed block comment");
    }

    std::optional<Token> nextToken() {
        const char c = peek();
        if (isIdentStart(c)) {
            return scanIdentifierOrKeyword();
        }
        if (isDigit(c)) {
            return scanNumber();
        }
        if (c == '\'') {
            return scanQuotedLiteral();
        }
        return scanOperatorOrPunct();
    }

    Token scanIdentifierOrKeyword() {
        const std::size_t begin = pos_;
        advance();
        while (!atEnd() && isIdentContinue(peek())) {
            advance();
        }
        const std::size_t end = pos_;
        const auto lexeme = text_.substr(begin, end - begin);
        return makeToken(lookupKeyword(lexeme), begin, end);
    }

    Token scanNumber() {
        const std::size_t begin = pos_;
        while (!atEnd() && isDigit(peek())) {
            advance();
        }

        bool isReal = false;

        // Real: '.' digit... but not '..'
        if (peek() == '.' && peek(1) != '.' && isDigit(peek(1))) {
            isReal = true;
            advance(); // '.'
            while (!atEnd() && isDigit(peek())) {
                advance();
            }
        }

        if (peek() == 'E' || peek() == 'e') {
            isReal = true;
            const std::size_t expMark = pos_;
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            if (!isDigit(peek())) {
                errorAt(expMark, "malformed real exponent");
                // Best-effort: keep what we have as a real/integer token.
            } else {
                while (!atEnd() && isDigit(peek())) {
                    advance();
                }
            }
        }

        return makeToken(isReal ? TokenKind::RealLiteral : TokenKind::IntegerLiteral, begin, pos_);
    }

    Token scanQuotedLiteral() {
        const std::size_t begin = pos_;
        advance(); // opening '

        std::string decoded;
        bool closed = false;

        while (!atEnd()) {
            const char c = peek();
            if (c == '\'') {
                advance();
                if (peek() == '\'') {
                    decoded.push_back('\'');
                    advance();
                    continue;
                }
                closed = true;
                break;
            }
            if (c == '\n' || c == '\r') {
                break;
            }
            decoded.push_back(c);
            advance();
        }

        if (!closed) {
            errorAt(begin, "unclosed string or character literal");
            // Recovery: already stopped at EOL/EOF.
        }

        const std::size_t end = pos_;
        const TokenKind kind =
            (closed && decoded.size() == 1) ? TokenKind::CharLiteral : TokenKind::StringLiteral;
        // Lexeme is the raw source span (including quotes), not decoded content.
        return makeToken(kind, begin, end);
    }

    std::optional<Token> scanOperatorOrPunct() {
        const std::size_t begin = pos_;

        switch (advance()) {
        case '+':
            return makeToken(TokenKind::Plus, begin, pos_);
        case '-':
            return makeToken(TokenKind::Minus, begin, pos_);
        case '*':
            return makeToken(TokenKind::Star, begin, pos_);
        case '/':
            return makeToken(TokenKind::Slash, begin, pos_);
        case '=':
            return makeToken(TokenKind::Equal, begin, pos_);
        case '[':
            return makeToken(TokenKind::LeftBracket, begin, pos_);
        case ']':
            return makeToken(TokenKind::RightBracket, begin, pos_);
        case ',':
            return makeToken(TokenKind::Comma, begin, pos_);
        case ';':
            return makeToken(TokenKind::Semicolon, begin, pos_);
        case '^':
            return makeToken(TokenKind::Caret, begin, pos_);
        case '@':
            return makeToken(TokenKind::At, begin, pos_);
        case '(':
            return makeToken(TokenKind::LeftParen, begin, pos_);
        case ')':
            return makeToken(TokenKind::RightParen, begin, pos_);
        case ':':
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::Assign, begin, pos_);
            }
            return makeToken(TokenKind::Colon, begin, pos_);
        case '<':
            if (peek() == '>') {
                advance();
                return makeToken(TokenKind::NotEqual, begin, pos_);
            }
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::LessEqual, begin, pos_);
            }
            return makeToken(TokenKind::Less, begin, pos_);
        case '>':
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::GreaterEqual, begin, pos_);
            }
            return makeToken(TokenKind::Greater, begin, pos_);
        case '.':
            if (peek() == '.') {
                advance();
                return makeToken(TokenKind::DotDot, begin, pos_);
            }
            return makeToken(TokenKind::Dot, begin, pos_);
        default:
            errorAt(begin, "unexpected character");
            return std::nullopt;
        }
    }
};

} // namespace

TokenStream scan(const SourceFile &source, DiagnosticEngine &diagnostics) {
    return ScannerImpl(source, diagnostics).run();
}

} // namespace apollo::pascal
