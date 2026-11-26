#pragma once
// First-launch import: copies the user's original Mad TV files (checked against the known release) and an
// upscale pack found next to them into the per-user directory.
#include "data/result.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace mtv {
using KnownFiles = std::map<std::string, std::string>; // upper-case DOS name -> SHA-256 (hex)
// The six files of the supported release (analysis/evidence/source-files.json).
const KnownFiles& known_release();
struct ImportReport { std::vector<std::string> files; bool pack = false; };
// Copies into <user_dir>/game (and <user_dir>/pack when <src>/pack or <src>/../pack holds a pack.json). The new
// copy is complete before it replaces the previous one; on any error the previous import is left as it was.
Result<ImportReport> import_game_data(const std::filesystem::path& src, const std::filesystem::path& user_dir,
                                      const KnownFiles& expected = known_release());
}
