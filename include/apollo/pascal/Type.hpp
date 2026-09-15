//
// Pascal type representation for Milestone 3 semantic analysis.
//

#ifndef APOLLO_PASCAL_TYPE_HPP
#define APOLLO_PASCAL_TYPE_HPP

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace apollo::pascal {

enum class TypeTag {
    Integer,
    Real,
    Boolean,
    Char,
    String,
    Alias,
    Array,
    Record,
    Error,
};

struct Type;

using TypePtr = std::shared_ptr<Type>;

struct RecordField {
    std::string name;
    TypePtr type;
};

struct Type {
    TypeTag tag{TypeTag::Error};
    std::string name;
    std::shared_ptr<Type> canonical; // Alias → underlying
    std::shared_ptr<Type> element;   // Array element
    /// Const index bounds for Array (Pascal `[lo..hi]`), evaluated by semantic analyse,
    /// which is the only producer of Array types — so `hasBounds` is never a guess.
    std::int64_t indexLow{0};
    std::int64_t indexHigh{0};
    bool hasBounds{false};
    std::vector<RecordField> fields;
};

[[nodiscard]] TypePtr makePredefined(TypeTag tag);
[[nodiscard]] TypePtr makeAlias(std::string name, TypePtr underlying);
[[nodiscard]] TypePtr makeArray(TypePtr element, std::int64_t indexLow, std::int64_t indexHigh);
[[nodiscard]] TypePtr makeRecord(std::vector<RecordField> fields);
[[nodiscard]] TypePtr makeError();

/// Peel Alias layers; returns Error if type is null.
[[nodiscard]] TypeTag canonicalTag(const Type &type);
[[nodiscard]] TypeTag canonicalTag(const TypePtr &type);

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_TYPE_HPP
