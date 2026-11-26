#include "rt/logic.h"
namespace mtv::rt {
extern const FunctionEntry mtv_functions[];
extern const size_t mtv_function_count;
const LogicTables& static_logic() {
    static const LogicTables t{kLogicAbi, "static", mtv_functions, mtv_function_count, kBlobModules, kBlobModuleCount};
    return t;
}
}
