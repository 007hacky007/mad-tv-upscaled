#include "platform/import.h"
#include "data/hash.h"
#include <algorithm>
#include <cctype>
namespace mtv {
namespace fs = std::filesystem;
const KnownFiles& known_release() {
    static const KnownFiles k{
        {"DATAV.PCK", "bdc33fd5f1e1e93af7f1eba6d5a0b1ea6f14d7d9fe783cde8798d54ccfddba70"},
        {"INTRO.EXE", "971bec0b50828b4f12ec6b38e621c0273a79b189be1cb73c2dfa943f318901a9"},
        {"MCV.BIN", "44632924c7642022e80f5ac5c899f60eda15dcac9c9f15a6a3738cbc7272c689"},
        {"MTV.EXE", "bb8f6e97922bbfa6b03ca3786afc179a03549b6ce647fbb2c1fe7f59a3e84b33"},
        {"SOURCE1.RSC", "e9d0d71a6ef8f463cd030aa266387d606936e4fac4a08a96cd8d8f5bd3182a2c"},
        {"SOURCE2.RSC", "83f95cff0cccb9a9b919b8a4f40ded7b5159cd1c008fb6d60c29161aa27ee54f"}};
    return k;
}
namespace {
std::string upper(std::string s) { for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; }
DataError fail(std::string why) { return DataError{"import", "", 0, std::move(why)}; }
// Replaces `target` with the finished directory `staged`, keeping the old one until the swap succeeded.
Result<void> swap_in(const fs::path& staged, const fs::path& target) {
    std::error_code ec;
    fs::path old = target;
    old += ".old";
    fs::remove_all(old, ec);
    if (fs::exists(target)) { fs::rename(target, old, ec); if (ec) return fail("cannot move the previous copy aside: " + ec.message()); }
    fs::rename(staged, target, ec);
    if (ec) { std::error_code ec2; if (fs::exists(old)) fs::rename(old, target, ec2); return fail("cannot put the new copy in place: " + ec.message()); }
    fs::remove_all(old, ec);
    return {};
}
}
Result<ImportReport> import_game_data(const fs::path& src, const fs::path& user_dir, const KnownFiles& expected) {
    std::map<std::string, fs::path> found;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(src, ec))
        if (e.is_regular_file()) found[upper(e.path().filename().string())] = e.path();
    if (ec) return fail(std::string("cannot read ") + reinterpret_cast<const char*>(src.u8string().c_str()) + ": " + ec.message());
    std::vector<std::string> missing;
    for (const auto& [name, hash] : expected) if (!found.count(name)) missing.push_back(name);
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        return fail("missing from the chosen folder: " + list);
    }
    for (const auto& [name, hash] : expected) {
        auto h = sha256_file(found[name]);
        if (!h.has_value()) return fail("cannot read " + name);
        if (h.value() != hash) return fail(name + " is not the known release (SHA-256 " + h.value() + ")");
    }
    fs::create_directories(user_dir, ec);
    if (ec) return fail("cannot create the user folder: " + ec.message());
    ImportReport report;
    const fs::path staged = user_dir / "game.importing";
    fs::remove_all(staged, ec);
    fs::create_directories(staged, ec);
    if (ec) return fail("cannot create " + std::string(reinterpret_cast<const char*>(staged.u8string().c_str())));
    for (const auto& [name, hash] : expected) {
        fs::copy_file(found[name], staged / name, fs::copy_options::overwrite_existing, ec);
        if (ec) { fs::remove_all(staged, ec); return fail("cannot copy " + name + ": " + ec.message()); }
        report.files.push_back(name);
    }
    if (auto r = swap_in(staged, user_dir / "game"); !r.has_value()) { fs::remove_all(staged, ec); return r.error(); }
    for (const fs::path& p : {src / "pack", src.parent_path() / "pack"}) {
        if (!fs::is_regular_file(p / "pack.json")) continue;
        const fs::path staged_pack = user_dir / "pack.importing";
        fs::remove_all(staged_pack, ec);
        fs::copy(p, staged_pack, fs::copy_options::recursive, ec);
        if (ec) { fs::remove_all(staged_pack, ec); return fail("cannot copy the upscale pack: " + ec.message()); }
        if (auto r = swap_in(staged_pack, user_dir / "pack"); !r.has_value()) return r.error();
        report.pack = true;
        break;
    }
    return report;
}
}
