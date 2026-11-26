#pragma once
// The mtv-logic library ships encrypted (logic.bin). The key comes from the player's own MTV.EXE and SOURCE1.RSC,
// so only someone with the original files can unlock it.
//
// Container: "MTVLOGIC", u32le format (1), u8 build id length, build id, 24-byte nonce, u64le plain size,
// ciphertext, 16-byte tag. XChaCha20-Poly1305; everything before the ciphertext is authenticated as well.
#include "data/result.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace mtv {
using LogicKey = std::array<uint8_t, 32>;
// BLAKE2b-256 keyed with "MadTV logic key v1" (zero padded to 32 bytes) over
// u64le(exe size) || exe || u64le(rsc size) || rsc.
LogicKey derive_logic_key(std::span<const std::byte> exe, std::span<const std::byte> rsc);
std::vector<std::byte> encrypt_logic(std::span<const std::byte> plain, const LogicKey& key, std::string_view build_id,
                                     std::span<const uint8_t, 24> nonce);
Result<std::vector<std::byte>> decrypt_logic(std::span<const std::byte> container, const LogicKey& key, std::string_view build_id);
}
