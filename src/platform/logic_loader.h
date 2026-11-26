#pragma once
// Loads the mtv-logic library (the translated game program) and checks that it belongs to this build.
#include "data/result.h"
#include "rt/logic.h"
#include <filesystem>
#include <memory>
#include <string_view>
struct SDL_SharedObject;
namespace mtv {
class LoadedLogic {
public:
    LoadedLogic(SDL_SharedObject* lib, const rt::LogicTables* tables);
    ~LoadedLogic();
    LoadedLogic(const LoadedLogic&) = delete;
    LoadedLogic& operator=(const LoadedLogic&) = delete;
    const rt::LogicTables& tables() const { return *tables_; }
private:
    SDL_SharedObject* lib_;
    const rt::LogicTables* tables_;
};
// The library's file name on this platform: mtv-logic.dylib or mtv-logic.dll.
const char* logic_library_name();
// The build id this program expects from the library (MTV_LOGIC_BUILD_ID).
std::string_view expected_logic_build_id();
Result<std::unique_ptr<LoadedLogic>> load_logic(const std::filesystem::path& lib, std::string_view build_id);
}
