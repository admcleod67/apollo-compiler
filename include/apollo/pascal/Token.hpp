//
// A single Pascal token with kind, range, and lexeme view.
//

#ifndef APOLLO_PASCAL_TOKEN_HPP
#define APOLLO_PASCAL_TOKEN_HPP

#pragma once

#include "apollo/common/SourceLocation.hpp"
#include "apollo/pascal/TokenKind.hpp"

#include <string_view>

namespace apollo::pascal {

struct Token {
    TokenKind kind{TokenKind::EndOfFile};
    apollo::common::SourceRange range{};
    /// View into SourceFile::text(); valid only while that SourceFile lives.
    std::string_view lexeme{};
};

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_TOKEN_HPP
