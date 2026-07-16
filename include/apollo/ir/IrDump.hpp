//
// Text dump of Apollo IR modules.
//

#ifndef APOLLO_IR_IR_DUMP_HPP
#define APOLLO_IR_IR_DUMP_HPP

#pragma once

#include "apollo/ir/Ir.hpp"

#include <iosfwd>

namespace apollo::ir {

/// Write an indented IR dump suitable for tests and future `apolloc --ir`.
void writeIrDump(std::ostream &out, const Module &module);

} // namespace apollo::ir

#endif // APOLLO_IR_IR_DUMP_HPP
