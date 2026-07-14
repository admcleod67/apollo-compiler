//
// Non-owning cursor over a TokenStream for recursive-descent parsing.
//

#ifndef APOLLO_PASCAL_TOKEN_CURSOR_HPP
#define APOLLO_PASCAL_TOKEN_CURSOR_HPP

#pragma once

#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/pascal/Token.hpp"
#include "apollo/pascal/TokenKind.hpp"
#include "apollo/pascal/TokenStream.hpp"

#include <cstddef>
#include <string_view>

namespace apollo::pascal {

class TokenCursor {
public:
    TokenCursor(const TokenStream &stream, apollo::common::DiagnosticEngine &diagnostics) noexcept;

    [[nodiscard]] const Token &current() const;
    [[nodiscard]] const Token &peek(std::size_t k = 0) const;

    [[nodiscard]] bool check(TokenKind kind) const;
    [[nodiscard]] bool match(TokenKind kind);
    const Token &advance();

    /// On mismatch: report an error at the current token and return false (do not advance).
    [[nodiscard]] bool expect(TokenKind kind, std::string_view message);

    /// Report an error at the current token without consuming it.
    void error(std::string_view message);

    [[nodiscard]] std::size_t index() const noexcept { return index_; }

private:
    [[nodiscard]] const Token &at(std::size_t index) const;

    const TokenStream *stream_;
    apollo::common::DiagnosticEngine *diagnostics_;
    std::size_t index_{0};
    /// Synthetic EOF used if the stream is empty (should not happen after a normal scan).
    Token eofFallback_{TokenKind::EndOfFile, {}, {}};
};

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_TOKEN_CURSOR_HPP
