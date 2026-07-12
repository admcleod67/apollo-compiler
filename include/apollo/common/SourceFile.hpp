//
// Source file buffer with stable line splitting for listings and later phases.
//

#ifndef APOLLO_COMMON_SOURCE_FILE_HPP
#define APOLLO_COMMON_SOURCE_FILE_HPP

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apollo::common {

/// Owns the full text of one compilation unit plus a display path.
///
/// Empty text yields zero lines. A lone trailing newline yields one empty line
/// (e.g. "\\n" -> lineCount() == 1, lineText(1) == "").
class SourceFile {
public:
    [[nodiscard]] static SourceFile fromString(std::string displayPath, std::string text);

    [[nodiscard]] const std::string &path() const noexcept { return path_; }
    [[nodiscard]] const std::string &text() const noexcept { return text_; }
    [[nodiscard]] std::size_t lineCount() const noexcept { return lineStarts_.size(); }

    /// 1-based line index. Returns empty view if out of range.
    [[nodiscard]] std::string_view lineText(std::size_t line) const;

private:
    SourceFile(std::string path, std::string text, std::vector<std::size_t> lineStarts);

    static std::vector<std::size_t> buildLineStarts(std::string_view text);

    std::string path_;
    std::string text_;
    std::vector<std::size_t> lineStarts_;
};

struct SourceFileLoadResult {
    std::optional<SourceFile> file;
    std::string error;
};

[[nodiscard]] SourceFileLoadResult loadSourceFile(std::string_view path);

} // namespace apollo::common

#endif // APOLLO_COMMON_SOURCE_FILE_HPP
