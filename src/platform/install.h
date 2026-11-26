#pragma once
// Unlocks the game logic: decrypts logic.bin with the key from the imported MTV.EXE and SOURCE1.RSC into
// <user_dir>/logic/<build id>/, once per build. Saves never touch <user_dir>/game, so this works again after an
// update.
#include "data/result.h"
#include "platform/import.h"
#include <filesystem>
#include <string_view>
namespace mtv {
Result<std::filesystem::path> ensure_logic(const std::filesystem::path& user_dir, const std::filesystem::path& logic_bin,
                                           std::string_view build_id, const KnownFiles& expected = known_release());
}
