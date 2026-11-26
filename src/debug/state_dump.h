#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace mtv {
// A checkpoint of game state as JSON (schema: schemas/state-dump.schema.json).
using StateDump = nlohmann::json;
// JSON pointers of every value that differs or exists on only one side, in document order of `a` then extra keys of `b`.
std::vector<std::string> compare_dumps(const StateDump& a, const StateDump& b);
}
