//
// Numbered source listing helper (language-agnostic).
//

#ifndef APOLLO_COMMON_LISTING_HPP
#define APOLLO_COMMON_LISTING_HPP

#pragma once

#include "apollo/common/SourceFile.hpp"

#include <iosfwd>

namespace apollo::common {

/// Write a numbered listing: right-aligned width-4 line number, ": ", line text, newline.
/// Empty source (lineCount() == 0) writes nothing.
void writeListing(std::ostream &out, const SourceFile &source);

} // namespace apollo::common

#endif // APOLLO_COMMON_LISTING_HPP
