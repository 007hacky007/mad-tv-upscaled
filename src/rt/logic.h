#pragma once
// The translated game logic as one set of tables: MTV.EXE's functions and the Palladix driver modules. Linked
// statically into tools and tests (static_logic) or loaded from the mtv-logic library at run time.
#include "rt/blobs.h"
#include <cstddef>
#include <cstdint>
namespace mtv::rt {
constexpr uint32_t kLogicAbi = 1;
struct LogicTables {
    uint32_t abi;
    const char* build_id;
    const FunctionEntry* functions;
    size_t function_count;
    const BlobModule* blobs;
    size_t blob_count;
};
const LogicTables& static_logic();
}
