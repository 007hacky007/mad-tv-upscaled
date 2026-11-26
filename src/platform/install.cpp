#include "platform/install.h"
#include "data/hash.h"
#include "platform/logic_crypto.h"
#include "platform/logic_loader.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
namespace mtv {
namespace fs = std::filesystem;
namespace {
std::string u8(const fs::path& p) { const auto s = p.u8string(); return std::string(s.begin(), s.end()); }
DataError fail(const fs::path& file, std::string why) { return DataError{u8(file), "", 0, std::move(why)}; }
bool read(const fs::path& p, std::vector<std::byte>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    out.resize(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return !f.bad();
}
}
Result<fs::path> ensure_logic(const fs::path& user_dir, const fs::path& logic_bin, std::string_view build_id, const KnownFiles& expected) {
    const fs::path root = user_dir / "logic";
    const fs::path dir = root / std::string(build_id);
    const fs::path lib = dir / logic_library_name();
    std::error_code ec;
    if (fs::is_regular_file(lib, ec)) return lib;
    std::vector<std::byte> exe, rsc, box;
    for (auto [name, buf] : {std::pair{"MTV.EXE", &exe}, std::pair{"SOURCE1.RSC", &rsc}}) {
        const fs::path p = user_dir / "game" / name;
        if (!read(p, *buf)) return fail(p, std::string("cannot read ") + name + "; import the original files again");
        if (auto it = expected.find(name); it == expected.end() || sha256(*buf) != it->second)
            return fail(p, std::string(name) + " in the user folder is not the known release; import the original files again");
    }
    if (!read(logic_bin, box)) return fail(logic_bin, "cannot read logic.bin (the installation is incomplete)");
    auto plain = decrypt_logic(box, derive_logic_key(exe, rsc), build_id);
    if (!plain.has_value()) return fail(logic_bin, plain.error().reason);
    fs::create_directories(dir, ec);
    if (ec) return fail(dir, "cannot create the folder: " + ec.message());
    fs::path tmp = lib;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(plain.value().data()), std::streamsize(plain.value().size()));
        f.close();
        if (!f) { fs::remove(tmp, ec); return fail(tmp, "cannot write the game logic (disk full?)"); }
    }
    fs::rename(tmp, lib, ec);
    if (ec) { std::error_code ec2; fs::remove(tmp, ec2); return fail(lib, "cannot put the game logic in place: " + ec.message()); }
    // earlier builds' libraries are no longer needed
    for (const auto& e : fs::directory_iterator(root, ec))
        if (e.is_directory() && e.path().filename() != dir.filename()) { std::error_code ec2; fs::remove_all(e.path(), ec2); }
    return lib;
}
}
