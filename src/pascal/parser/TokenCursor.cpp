#include "apollo/pascal/TokenCursor.hpp"

#include "apollo/common/Diagnostic.hpp"

namespace apollo::pascal {

TokenCursor::TokenCursor(const TokenStream &stream,
                         apollo::common::DiagnosticEngine &diagnostics) noexcept
    : stream_(&stream), diagnostics_(&diagnostics) {}

const Token &TokenCursor::at(std::size_t index) const {
    if (stream_->empty()) {
        return eofFallback_;
    }
    if (index >= stream_->size()) {
        return (*stream_)[stream_->size() - 1];
    }
    return (*stream_)[index];
}

const Token &TokenCursor::current() const {
    return at(index_);
}

const Token &TokenCursor::peek(std::size_t k) const {
    return at(index_ + k);
}

bool TokenCursor::check(TokenKind kind) const {
    return current().kind == kind;
}

bool TokenCursor::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

const Token &TokenCursor::advance() {
    if (!stream_->empty() && index_ + 1 < stream_->size()) {
        ++index_;
    }
    return current();
}

bool TokenCursor::expect(TokenKind kind, std::string_view message) {
    if (check(kind)) {
        advance();
        return true;
    }
    diagnostics_->report(apollo::common::DiagnosticSeverity::Error, current().range.begin,
                         std::string(message));
    return false;
}

void TokenCursor::error(std::string_view message) {
    diagnostics_->report(apollo::common::DiagnosticSeverity::Error, current().range.begin,
                         std::string(message));
}

} // namespace apollo::pascal
