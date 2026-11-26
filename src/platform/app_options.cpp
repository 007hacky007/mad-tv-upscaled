#include "platform/app.h"
#include <charconv>
namespace mtv {
Result<AppOptions> parse_app_options(std::span<const std::string_view> args) {
    AppOptions o;
    auto error = [](std::size_t i, std::string reason) -> Result<AppOptions> { return DataError{"arguments", "", i, std::move(reason)}; };
    auto value = [&](std::size_t& i) -> std::string_view { return ++i < args.size() ? args[i] : std::string_view{}; };
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto a = args[i];
        if (a == "--smoke-test") o.smoke_test = true;
        else if (a == "--intro") o.intro = true;
        else if (a == "--realtime") o.realtime = true;
        else if (a == "--build-pack") o.build_pack = true;
        else if (a == "--frames") {
            auto s = value(i);
            auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), o.frames);
            if (s.empty() || ec != std::errc{} || p != s.data() + s.size() || o.frames == 0) return error(i, "frame count must be a positive integer");
        } else if (a == "--report" || a == "--data" || a == "--screenshot" || a == "--script" || a == "--pack" || a == "--import") {
            auto s = value(i);
            if (s.empty()) return error(i, "missing path");
            (a == "--report" ? o.report_path : a == "--data" ? o.data_dir : a == "--screenshot" ? o.screenshot_path : a == "--pack" ? o.pack_dir : a == "--import" ? o.import_dir : o.script_path) = std::string(s);
        } else return error(i, "unknown option");
    }
    return o;
}
}
