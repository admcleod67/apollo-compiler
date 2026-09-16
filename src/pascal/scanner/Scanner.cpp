#include "apollo/pascal/Scanner.hpp"

#include "apollo/common/Diagnostic.hpp"
#include "apollo/common/SourceLocation.hpp"

#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace apollo::pascal {
namespace {

using apollo::common::DiagnosticEngine;
using apollo::common::DiagnosticSeverity;
using apollo::common::SourceFile;
using apollo::common::SourceLocation;
using apollo::common::SourceRange;

constexpr std::size_t kMaxIncludeDepth = 32;

std::string canonicalPathKey(std::string_view path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(fs::path(path), ec);
    if (ec) {
        return std::string(path);
    }
    return canonical.string();
}

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

std::string trimAscii(std::string_view text) {
    while (!text.empty() && isWhitespace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isWhitespace(text.back())) {
        text.remove_suffix(1);
    }
    return std::string(text);
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
    ScannerImpl(ScanResult &result, DiagnosticEngine &diagnostics)
        : result_(&result), diagnostics_(&diagnostics) {
        pushFrame(*result.sources.front());
    }

    TokenStream run() {
        TokenStream stream;
        while (true) {
            skipWhitespaceAndComments();
            if (atEnd()) {
                if (!popFrame()) {
                    break;
                }
                continue;
            }
            if (auto token = nextToken()) {
                stream.push_back(*token);
            }
        }
        stream.push_back(makeToken(TokenKind::EndOfFile, pos_, pos_));
        return stream;
    }

private:
    struct Frame {
        const SourceFile *source{};
        std::string_view text{};
        std::size_t pos{0};
        std::string path;
        std::string canonicalKey;
    };

    ScanResult *result_;
    DiagnosticEngine *diagnostics_;
    std::vector<Frame> stack_;
    std::unordered_set<std::string> activeCanonicalPaths_;

    const SourceFile *source_{nullptr};
    std::string_view text_{};
    std::size_t pos_{0};

    void pushFrame(const SourceFile &source) {
        const std::string key = canonicalPathKey(source.path());
        stack_.push_back(Frame{&source, source.text(), 0, source.path(), key});
        activeCanonicalPaths_.insert(key);
        applyTopFrame();
        diagnostics_->setActivePath(source.path());
    }

    bool popFrame() {
        if (stack_.size() <= 1) {
            return false;
        }
        activeCanonicalPaths_.erase(stack_.back().canonicalKey);
        stack_.pop_back();
        applyTopFrame();
        diagnostics_->setActivePath(stack_.back().path);
        return true;
    }

    void applyTopFrame() {
        Frame &top = stack_.back();
        source_ = top.source;
        text_ = top.text;
        pos_ = top.pos;
    }

    void saveTopPos() {
        stack_.back().pos = pos_;
    }

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

    [[nodiscard]] SourceLocation loc(const std::size_t offset) const {
        return source_->locationAt(offset);
    }

    [[nodiscard]] SourceRange range(const std::size_t begin, const std::size_t end) const {
        return SourceRange{loc(begin), loc(end)};
    }

    [[nodiscard]] Token makeToken(const TokenKind kind, const std::size_t begin,
                                  const std::size_t end) const {
        return Token{kind, range(begin, end), text_.substr(begin, end - begin)};
    }

    void errorAt(const std::size_t offset, std::string message) const {
        diagnostics_->report(DiagnosticSeverity::Error, loc(offset), std::move(message));
    }

    void warnAt(const std::size_t offset, std::string message) const {
        diagnostics_->report(DiagnosticSeverity::Warning, loc(offset), std::move(message));
    }

    void skipWhitespaceAndComments() {
        while (!atEnd()) {
            const char c = peek();
            if (isWhitespace(c)) {
                advance();
                continue;
            }
            if (c == '{') {
                if (peek(1) == '$') {
                    handleDirective();
                } else {
                    skipBraceComment();
                }
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

    void handleDirective() {
        const std::size_t start = pos_;
        advance(); // '{'
        advance(); // '$'

        std::string body;
        bool closed = false;
        while (!atEnd()) {
            if (peek() == '}') {
                advance();
                closed = true;
                break;
            }
            body.push_back(advance());
        }
        if (!closed) {
            errorAt(start, "unclosed compiler directive");
            return;
        }

        std::string_view rest(body);
        while (!rest.empty() && isWhitespace(rest.front())) {
            rest.remove_prefix(1);
        }
        if (rest.empty() || !isAsciiLetter(rest.front())) {
            warnAt(start, "unknown compiler directive");
            return;
        }

        const char letter = toLowerAscii(rest.front());
        rest.remove_prefix(1);

        if (letter == 'i') {
            if (!rest.empty() && (rest.front() == '+' || rest.front() == '-')) {
                errorAt(start, "{$I+} / {$I-} forms are not supported");
                return;
            }
            handleInclude(start, trimAscii(rest));
            return;
        }

        warnAt(start, "unknown compiler directive");
    }

    void handleInclude(std::size_t directiveOffset, std::string filename) {
        if (filename.empty()) {
            errorAt(directiveOffset, "{$I} requires a file name");
            return;
        }
        if ((filename.front() == '\'' && filename.back() == '\'') ||
            (filename.front() == '"' && filename.back() == '"')) {
            if (filename.size() < 2) {
                errorAt(directiveOffset, "{$I} requires a file name");
                return;
            }
            filename = filename.substr(1, filename.size() - 2);
            filename = trimAscii(filename);
        }
        if (filename.empty()) {
            errorAt(directiveOffset, "{$I} requires a file name");
            return;
        }

        // Nesting depth: root is frame 0; at most kMaxIncludeDepth includes.
        if (stack_.size() > kMaxIncludeDepth) {
            errorAt(directiveOffset, "include nesting depth exceeds limit of 32");
            return;
        }

        const auto resolved =
            apollo::common::resolveIncludePath(stack_.back().path, filename);
        if (!resolved.path) {
            errorAt(directiveOffset, resolved.error);
            return;
        }
        const std::string &canonical = *resolved.path;
        if (activeCanonicalPaths_.count(canonical) != 0) {
            errorAt(directiveOffset, "cyclic include of '" + canonical + "'");
            return;
        }

        auto loaded = apollo::common::loadSourceFile(canonical);
        if (!loaded.file) {
            errorAt(directiveOffset, loaded.error);
            return;
        }

        saveTopPos();
        result_->sources.push_back(
            std::make_unique<SourceFile>(std::move(*loaded.file)));
        // Prefer display path as the resolved canonical path for diagnostics.
        SourceFile &owned = *result_->sources.back();
        pushFrame(owned);
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

        if (peek() == '.' && peek(1) != '.' && isDigit(peek(1))) {
            isReal = true;
            advance();
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
        advance();

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
        }

        const std::size_t end = pos_;
        const TokenKind kind =
            (closed && decoded.size() == 1) ? TokenKind::CharLiteral : TokenKind::StringLiteral;
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

ScanResult scan(const SourceFile &source, DiagnosticEngine &diagnostics) {
    ScanResult result;
    result.sources.push_back(
        std::make_unique<SourceFile>(SourceFile::fromString(source.path(), source.text())));
    diagnostics.setActivePath(result.sources.front()->path());
    result.tokens = ScannerImpl(result, diagnostics).run();
    diagnostics.setActivePath(result.sources.front()->path());
    return result;
}

} // namespace apollo::pascal
