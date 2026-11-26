#include "extract.h"
namespace mtv::tool { int encrypt_logic_command(std::span<const std::string_view> args); int build_pack_command(std::span<const std::string_view> args); }
#include <iostream>
#include <string_view>
#include <vector>
int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) { std::cerr << "usage: mtv-tool <command> ...\ncommands: unpack, build-pack, encrypt-logic, unpack-pp20, classify, export-images, viewer, coverage\n"; return 2; }
    auto rest = std::span(args).subspan(1);
    if (args[0] == "unpack") return mtv::tool::unpack_command(rest);
    if (args[0] == "unpack-pp20") return mtv::tool::unpack_pp20_command(rest);
    if (args[0] == "intro-frames") return mtv::tool::intro_frames_command(rest);
#ifdef MTV_HAVE_GAME
    if (args[0] == "run-game") return mtv::tool::run_game_command(rest);
    if (args[0] == "call-fn") return mtv::tool::call_fn_command(rest);
#endif
    if (args[0] == "build-pack") return mtv::tool::build_pack_command(rest);
    if (args[0] == "encrypt-logic") return mtv::tool::encrypt_logic_command(rest);
    if (args[0] == "classify") return mtv::tool::classify_command(rest);
    if (args[0] == "export-images") return mtv::tool::export_images_command(rest);
    if (args[0] == "viewer") return mtv::tool::viewer_command(rest);
    if (args[0] == "coverage") return mtv::tool::coverage_command(rest);
    std::cerr << "unknown command " << args[0] << '\n';
    return 2;
}
