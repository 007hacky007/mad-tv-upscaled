#pragma once
#include "data/result.h"
#include <filesystem>
namespace mtv {
std::string sha256(ByteView bytes);
Result<std::string> sha256_file(const std::filesystem::path& path);
}
