//
// Ordered sequence of Pascal tokens from one scan.
//

#ifndef APOLLO_PASCAL_TOKEN_STREAM_HPP
#define APOLLO_PASCAL_TOKEN_STREAM_HPP

#pragma once

#include "apollo/pascal/Token.hpp"

#include <cstddef>
#include <vector>

namespace apollo::pascal {

class TokenStream {
public:
    TokenStream() = default;

    explicit TokenStream(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    [[nodiscard]] std::size_t size() const noexcept { return tokens_.size(); }
    [[nodiscard]] bool empty() const noexcept { return tokens_.empty(); }

    [[nodiscard]] const Token &operator[](std::size_t index) const { return tokens_[index]; }
    [[nodiscard]] Token &operator[](std::size_t index) { return tokens_[index]; }

    [[nodiscard]] auto begin() const noexcept { return tokens_.begin(); }
    [[nodiscard]] auto end() const noexcept { return tokens_.end(); }
    [[nodiscard]] auto begin() noexcept { return tokens_.begin(); }
    [[nodiscard]] auto end() noexcept { return tokens_.end(); }

    void push_back(Token token) { tokens_.push_back(std::move(token)); }

private:
    std::vector<Token> tokens_;
};

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_TOKEN_STREAM_HPP
