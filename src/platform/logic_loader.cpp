#include "platform/logic_loader.h"
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_error.h>
#if __has_include("logic_build_id.h")
#include "logic_build_id.h" // generated with the translation: MTV_LOGIC_BUILD_ID
#else
#define MTV_LOGIC_BUILD_ID ""
#endif
namespace mtv {
LoadedLogic::LoadedLogic(SDL_SharedObject* lib, const rt::LogicTables* tables) : lib_(lib), tables_(tables) {}
LoadedLogic::~LoadedLogic() { if (lib_) SDL_UnloadObject(lib_); }
const char* logic_library_name() {
#ifdef _WIN32
    return "mtv-logic.dll";
#else
    return "mtv-logic.dylib";
#endif
}
std::string_view expected_logic_build_id() { return MTV_LOGIC_BUILD_ID; }
Result<std::unique_ptr<LoadedLogic>> load_logic(const std::filesystem::path& lib, std::string_view build_id) {
    std::error_code aec;
    const auto abs = std::filesystem::absolute(lib, aec);
    const auto name = (aec ? lib : abs).u8string();
    const std::string path(name.begin(), name.end());
    auto fail = [&](std::string why) { return DataError{path, "", 0, std::move(why)}; };
    std::error_code ec;
    if (!std::filesystem::is_regular_file(lib, ec)) return fail("the game logic library is missing");
    SDL_SharedObject* so = SDL_LoadObject(path.c_str());
    if (!so) return fail(std::string("cannot load the game logic library: ") + SDL_GetError());
    using Get = const rt::LogicTables* (*)(uint32_t);
    auto get = reinterpret_cast<Get>(SDL_LoadFunction(so, "mtv_logic_get"));
    if (!get) { SDL_UnloadObject(so); return fail("not a game logic library (mtv_logic_get missing)"); }
    const rt::LogicTables* t = get(rt::kLogicAbi);
    if (!t || t->abi != rt::kLogicAbi) { SDL_UnloadObject(so); return fail("game logic library has another interface version"); }
    if (build_id != t->build_id) {
        auto e = fail("game logic library belongs to another build (" + std::string(t->build_id) + ", expected " + std::string(build_id) + ")");
        SDL_UnloadObject(so); // t points into the library: read it first
        return e;
    }
    return std::make_unique<LoadedLogic>(so, t);
}
}
