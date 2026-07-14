//
// Indented text dump of a Pascal Program AST.
//

#ifndef APOLLO_PASCAL_AST_DUMP_HPP
#define APOLLO_PASCAL_AST_DUMP_HPP

#pragma once

#include "apollo/pascal/ast/Ast.hpp"

#include <iosfwd>

namespace apollo::pascal {

/// Write an indented AST dump suitable for tests and `apolloc --ast`.
void writeAstDump(std::ostream &out, const ast::Program &program);

} // namespace apollo::pascal

#endif // APOLLO_PASCAL_AST_DUMP_HPP
