//
// Source locations and ranges (1-based line/column, 0-based byte offset).
//

#ifndef APOLLO_COMMON_SOURCE_LOCATION_HPP
#define APOLLO_COMMON_SOURCE_LOCATION_HPP

#pragma once

#include <cstddef>

namespace apollo::common {

struct SourceLocation {
    std::size_t line{1};   // 1-based
    std::size_t column{1}; // 1-based
    std::size_t offset{0}; // 0-based byte index into SourceFile::text()

    friend bool operator==(const SourceLocation &a, const SourceLocation &b) noexcept {
        return a.line == b.line && a.column == b.column && a.offset == b.offset;
    }

    friend bool operator!=(const SourceLocation &a, const SourceLocation &b) noexcept {
        return !(a == b);
    }
};

/// Half-open range: begin inclusive, end exclusive (for later token spans).
struct SourceRange {
    SourceLocation begin{};
    SourceLocation end{};

    friend bool operator==(const SourceRange &a, const SourceRange &b) noexcept {
        return a.begin == b.begin && a.end == b.end;
    }

    friend bool operator!=(const SourceRange &a, const SourceRange &b) noexcept {
        return !(a == b);
    }
};

} // namespace apollo::common

#endif // APOLLO_COMMON_SOURCE_LOCATION_HPP
