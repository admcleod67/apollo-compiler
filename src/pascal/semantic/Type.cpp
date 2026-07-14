#include "apollo/pascal/Type.hpp"

#include <utility>

namespace apollo::pascal {

TypePtr makePredefined(TypeTag tag) {
    auto type = std::make_shared<Type>();
    type->tag = tag;
    switch (tag) {
    case TypeTag::Integer:
        type->name = "integer";
        break;
    case TypeTag::Real:
        type->name = "real";
        break;
    case TypeTag::Boolean:
        type->name = "boolean";
        break;
    case TypeTag::Char:
        type->name = "char";
        break;
    case TypeTag::String:
        type->name = "string";
        break;
    default:
        type->tag = TypeTag::Error;
        type->name = "<error>";
        break;
    }
    return type;
}

TypePtr makeAlias(std::string name, TypePtr underlying) {
    auto type = std::make_shared<Type>();
    type->tag = TypeTag::Alias;
    type->name = std::move(name);
    type->canonical = std::move(underlying);
    return type;
}

TypePtr makeArray(TypePtr element) {
    auto type = std::make_shared<Type>();
    type->tag = TypeTag::Array;
    type->element = std::move(element);
    return type;
}

TypePtr makeError() {
    auto type = std::make_shared<Type>();
    type->tag = TypeTag::Error;
    type->name = "<error>";
    return type;
}

TypeTag canonicalTag(const Type &type) {
    if (type.tag == TypeTag::Alias) {
        if (!type.canonical) {
            return TypeTag::Error;
        }
        return canonicalTag(*type.canonical);
    }
    return type.tag;
}

TypeTag canonicalTag(const TypePtr &type) {
    if (!type) {
        return TypeTag::Error;
    }
    return canonicalTag(*type);
}

} // namespace apollo::pascal
