#include "apollo/common/SourceFile.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace apollo::common {

SourceFile::SourceFile(std::string path, std::string text, std::vector<std::size_t> lineStarts)
    : path_(std::move(path)), text_(std::move(text)), lineStarts_(std::move(lineStarts)) {}

std::vector<std::size_t> SourceFile::buildLineStarts(std::string_view text) {
    std::vector<std::size_t> starts;
    if (text.empty()) {
        return starts;
    }

    starts.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            if (i + 1 < text.size()) {
                starts.push_back(i + 1);
            }
        } else if (c == '\n') {
            if (i + 1 < text.size()) {
                starts.push_back(i + 1);
            }
        }
    }
    return starts;
}

SourceFile SourceFile::fromString(std::string displayPath, std::string text) {
    auto starts = buildLineStarts(text);
    return {std::move(displayPath), std::move(text), std::move(starts)};
}

SourceFileLoadResult loadSourceFile(std::string_view path) {
    SourceFileLoadResult result;
    const std::string pathStr(path);
    std::ifstream in(pathStr, std::ios::binary);
    if (!in) {
        result.error = "cannot open file: " + pathStr;
        return result;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        result.error = "cannot read file: " + pathStr;
        return result;
    }

    result.file = SourceFile::fromString(pathStr, buffer.str());
    return result;
}

std::string_view SourceFile::lineText(std::size_t line) const {
    if (line == 0 || line > lineStarts_.size()) {
        return {};
    }

    const std::size_t start = lineStarts_[line - 1];
    std::size_t end = text_.size();
    if (line < lineStarts_.size()) {
        end = lineStarts_[line];
    }

    // Strip the newline sequence that terminated this line (if any).
    if (end > start && text_[end - 1] == '\n') {
        --end;
        if (end > start && text_[end - 1] == '\r') {
            --end;
        }
    } else if (end > start && text_[end - 1] == '\r') {
        --end;
    }

    return std::string_view(text_).substr(start, end - start);
}

std::size_t SourceFile::lineStartOffset(std::size_t line) const noexcept {
    if (line == 0 || line > lineStarts_.size()) {
        return std::string::npos;
    }
    return lineStarts_[line - 1];
}

SourceLocation SourceFile::locationAt(std::size_t offset) const {
    if (text_.empty()) {
        return SourceLocation{1, 1, 0};
    }

    if (offset > text_.size()) {
        offset = text_.size();
    }

    // Greatest lineStarts_[i] <= offset.
    const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    const auto index = static_cast<std::size_t>(it - lineStarts_.begin());
    const std::size_t lineIndex = index == 0 ? 0 : index - 1;
    const std::size_t start = lineStarts_[lineIndex];

    return SourceLocation{
        lineIndex + 1,
        offset - start + 1,
        offset,
    };
}

} // namespace apollo::common
