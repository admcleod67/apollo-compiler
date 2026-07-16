#include "apollo/ir/Ir.hpp"

namespace apollo::ir {

ValueId Function::newTemp() {
    return ValueId{nextTemp++};
}

} // namespace apollo::ir
