#include "debug/state_dump.h"
namespace mtv {
namespace {
std::string escape(const std::string& k) {
    std::string out;
    for (char c : k) { if (c == '~') out += "~0"; else if (c == '/') out += "~1"; else out += c; }
    return out;
}
void diff(const StateDump& a, const StateDump& b, const std::string& path, std::vector<std::string>& out) {
    if (a.is_object() && b.is_object()) {
        for (auto it = a.begin(); it != a.end(); ++it) {
            auto p = path + "/" + escape(it.key());
            if (!b.contains(it.key())) out.push_back(p);
            else diff(it.value(), b.at(it.key()), p, out);
        }
        for (auto it = b.begin(); it != b.end(); ++it)
            if (!a.contains(it.key())) out.push_back(path + "/" + escape(it.key()));
        return;
    }
    if (a.is_array() && b.is_array()) {
        std::size_t n = std::max(a.size(), b.size());
        for (std::size_t i = 0; i < n; ++i) {
            auto p = path + "/" + std::to_string(i);
            if (i >= a.size() || i >= b.size()) out.push_back(p);
            else diff(a[i], b[i], p, out);
        }
        return;
    }
    if (a != b) out.push_back(path.empty() ? "/" : path);
}
}
std::vector<std::string> compare_dumps(const StateDump& a, const StateDump& b) {
    std::vector<std::string> out;
    diff(a, b, "", out);
    return out;
}
}
