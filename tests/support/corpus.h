#pragma once
// Corpus tests read the user's own game files. They fail (never skip) when the data is missing.
#include <doctest/doctest.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
namespace mtv::test {
// The source tree (reference data, fixtures): MTV_SOURCE overrides the build machine's path (Windows VM runs).
inline std::filesystem::path source_dir() {
    if (const char* env = std::getenv("MTV_SOURCE")) return env;
    return MTV_SOURCE_DIR;
}
inline std::filesystem::path corpus_dir() {
    if (const char* env = std::getenv("MTV_DATA")) return env;
    return source_dir() / "mad-tv";
}
inline std::vector<uint8_t> read_corpus_file(const std::string& name) {
    std::ifstream f(corpus_dir() / name, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "corpus file missing: " << (corpus_dir() / name).string() << " (set MTV_DATA)");
    return {std::istreambuf_iterator<char>(f), {}};
}
}
