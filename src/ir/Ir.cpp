#include "apollo/ir/Ir.hpp"

namespace apollo::ir {

ValueId Function::newTemp() {
    return ValueId{nextTemp++};
}

bool producesValue(const Instr &instr) {
    switch (instr.op) {
    case Op::StoreLocal:
    case Op::StoreIndex:
    case Op::DimArray:
    case Op::ArrayCopy:
        return false;
    case Op::Call:
    case Op::CallRuntime:
        return instr.type != IrType::Void;
    default:
        return true;
    }
}

} // namespace apollo::ir
