// The one symbol the mtv-logic library exports: its tables, for the ABI the caller was built with.
#include "rt/logic.h"
#ifdef _WIN32
#define MTV_EXPORT __declspec(dllexport)
#else
#define MTV_EXPORT __attribute__((visibility("default")))
#endif
#include "logic_build_id.h" // generated: MTV_LOGIC_BUILD_ID
namespace mtv::rt {
extern const FunctionEntry mtv_functions[];
extern const size_t mtv_function_count;
}
extern "C" MTV_EXPORT const mtv::rt::LogicTables* mtv_logic_get(uint32_t abi) {
    using namespace mtv::rt;
    static const LogicTables t{kLogicAbi, MTV_LOGIC_BUILD_ID, mtv_functions, mtv_function_count, kBlobModules, kBlobModuleCount};
    return abi == kLogicAbi ? &t : nullptr;
}
