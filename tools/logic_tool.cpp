// mtv-tool encrypt-logic --data <game dir> --in <mtv-logic library> --out <logic.bin> [--build-id <id>]
// Encrypts the library with the key from the game's MTV.EXE and SOURCE1.RSC (checked against the known release).
#include "data/hash.h"
#include "platform/import.h"
#include "platform/logic_crypto.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif
#if __has_include("logic_build_id.h")
#include "logic_build_id.h" // generated with the translation: MTV_LOGIC_BUILD_ID
#else
#define MTV_LOGIC_BUILD_ID ""
#endif
namespace mtv::tool {
namespace {
bool read(const std::filesystem::path& p, std::vector<std::byte>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::vector<char> c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    out.resize(c.size());
    std::memcpy(out.data(), c.data(), c.size());
    return true;
}
bool random_nonce(std::array<uint8_t, 24>& n) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, n.data(), ULONG(n.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    arc4random_buf(n.data(), n.size());
    return true;
#endif
}
}
int encrypt_logic_command(std::span<const std::string_view> args) {
    std::string data, in, out, build_id = MTV_LOGIC_BUILD_ID;
    for (size_t i = 0; i + 1 < args.size(); i += 2) {
        if (args[i] == "--data") data = args[i + 1];
        else if (args[i] == "--in") in = args[i + 1];
        else if (args[i] == "--out") out = args[i + 1];
        else if (args[i] == "--build-id") build_id = args[i + 1];
        else { std::cerr << "unknown option " << args[i] << '\n'; return 2; }
    }
    if (data.empty() || in.empty() || out.empty() || build_id.empty() || args.size() % 2) {
        std::cerr << "usage: mtv-tool encrypt-logic --data <game dir> --in <library> --out <logic.bin> [--build-id <id>]\n";
        return 2;
    }
    std::vector<std::byte> exe, rsc, lib;
    for (auto [name, buf] : {std::pair{"MTV.EXE", &exe}, std::pair{"SOURCE1.RSC", &rsc}}) {
        const auto p = std::filesystem::path(data) / name;
        if (!read(p, *buf)) { std::cerr << "cannot read " << p.string() << '\n'; return 1; }
        if (sha256(*buf) != known_release().at(name)) { std::cerr << p.string() << " is not the supported release\n"; return 1; }
    }
    if (!read(in, lib)) { std::cerr << "cannot read " << in << '\n'; return 1; }
    std::array<uint8_t, 24> nonce;
    if (!random_nonce(nonce)) { std::cerr << "no random source\n"; return 1; }
    const auto box = encrypt_logic(lib, derive_logic_key(exe, rsc), build_id, nonce);
    std::ofstream f(out, std::ios::binary);
    f.write(reinterpret_cast<const char*>(box.data()), std::streamsize(box.size()));
    if (!f) { std::cerr << "cannot write " << out << '\n'; return 1; }
    std::cout << "logic.bin: " << box.size() << " bytes, build " << build_id << '\n';
    return 0;
}
}
