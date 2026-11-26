#pragma once
#include "rt/machine.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace mtv::rt {
// Code the game loads at run time (Palladix drivers), recognised by its first 64 bytes.
struct BlobModule { uint16_t slot; std::array<uint8_t, 64> signature; const FunctionEntry* functions; size_t count; };
extern const BlobModule kBlobModules[];
extern const size_t kBlobModuleCount;
}
