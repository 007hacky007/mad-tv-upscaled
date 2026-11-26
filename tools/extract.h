#pragma once
#include <span>
#include <string_view>
namespace mtv::tool {
int unpack_command(std::span<const std::string_view> args);
int unpack_pp20_command(std::span<const std::string_view> args);
int intro_frames_command(std::span<const std::string_view> args);
int run_game_command(std::span<const std::string_view> args);
int call_fn_command(std::span<const std::string_view> args);
int classify_command(std::span<const std::string_view> args);
int export_images_command(std::span<const std::string_view> args);
int viewer_command(std::span<const std::string_view> args);
int coverage_command(std::span<const std::string_view> args);
}
