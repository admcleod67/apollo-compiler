//
// Token kind names and token-stream dump formatting.
//

#ifndef APOLLO_PASCAL_TOKEN_DUMP_HPP
#define APOLLO_PASCAL_TOKEN_DUMP_HPP

#pragma once

#include "apollo/pascal/TokenKind.hpp"
#include "apollo/pascal/TokenStream.hpp"

#include <iosfwd>

namespace apollo::pascal {

/// Enumerator name without the TokenKind:: prefix (e.g. "KeywordProgram").
[[nodiscard]] const char *tokenKindName(TokenKind kind) noexcept;

/// Write one line per token: line:col-endLine:endCol  KindName  lexeme
void writeTokenDump(std::ostream &out, const TokenStream &stream);

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_TOKEN_DUMP_HPP
