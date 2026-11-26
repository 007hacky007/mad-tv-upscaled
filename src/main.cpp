#include "platform/app.h"
#include <iostream>
#include <vector>
int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    auto result = mtv::parse_app_options(args);
    if (!result.has_value()) { std::cerr << result.error().reason << '\n'; return 2; }
    return mtv::run_app(result.value());
}
