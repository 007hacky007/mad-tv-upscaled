#include "platform/logic_crypto.h"
#include <cstring>
#include <string>
extern "C" {
#include "monocypher.h"
}
namespace mtv {
namespace {
constexpr char kMagic[8] = {'M', 'T', 'V', 'L', 'O', 'G', 'I', 'C'};
constexpr uint32_t kFormat = 1;
void put_u64(std::vector<uint8_t>& out, uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(uint8_t(v >> (8 * i))); }
uint64_t get_u64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = v << 8 | p[i]; return v; }
const uint8_t* u8(std::span<const std::byte> s) { return reinterpret_cast<const uint8_t*>(s.data()); }
// Header up to and including the plain size; it is the associated data.
std::vector<uint8_t> header(std::string_view build_id, std::span<const uint8_t, 24> nonce, uint64_t size) {
    std::vector<uint8_t> h(kMagic, kMagic + 8);
    for (int i = 0; i < 4; ++i) h.push_back(uint8_t(kFormat >> (8 * i)));
    h.push_back(uint8_t(build_id.size()));
    h.insert(h.end(), build_id.begin(), build_id.end());
    h.insert(h.end(), nonce.begin(), nonce.end());
    put_u64(h, size);
    return h;
}
DataError refused(std::string why) { return DataError{"logic.bin", "", 0, std::move(why)}; }
}
LogicKey derive_logic_key(std::span<const std::byte> exe, std::span<const std::byte> rsc) {
    uint8_t domain[32] = {};
    std::memcpy(domain, "MadTV logic key v1", 18);
    crypto_blake2b_ctx ctx;
    crypto_blake2b_keyed_init(&ctx, 32, domain, sizeof domain);
    for (auto part : {exe, rsc}) {
        std::vector<uint8_t> len;
        put_u64(len, part.size());
        crypto_blake2b_update(&ctx, len.data(), len.size());
        crypto_blake2b_update(&ctx, u8(part), part.size());
    }
    LogicKey k;
    crypto_blake2b_final(&ctx, k.data());
    return k;
}
std::vector<std::byte> encrypt_logic(std::span<const std::byte> plain, const LogicKey& key, std::string_view build_id,
                                     std::span<const uint8_t, 24> nonce) {
    auto h = header(build_id.substr(0, 255), nonce, plain.size());
    const size_t at = h.size();
    h.resize(at + plain.size() + 16);
    crypto_aead_lock(h.data() + at, h.data() + at + plain.size(), key.data(), nonce.data(), h.data(), at, u8(plain), plain.size());
    std::vector<std::byte> out(h.size());
    std::memcpy(out.data(), h.data(), h.size());
    return out;
}
Result<std::vector<std::byte>> decrypt_logic(std::span<const std::byte> container, const LogicKey& key, std::string_view build_id) {
    const uint8_t* p = u8(container);
    const size_t n = container.size();
    if (n < 13 || std::memcmp(p, kMagic, 8) != 0) return refused("not a Mad TV logic file");
    const uint32_t format = uint32_t(p[8]) | uint32_t(p[9]) << 8 | uint32_t(p[10]) << 16 | uint32_t(p[11]) << 24;
    if (format != kFormat) return refused("unknown logic file format " + std::to_string(format));
    const size_t id_len = p[12];
    const size_t at = 13 + id_len + 24 + 8;
    if (n < at + 16) return refused("logic file is truncated");
    const std::string_view id(reinterpret_cast<const char*>(p + 13), id_len);
    const uint64_t size = get_u64(p + 13 + id_len + 24);
    if (size != n - at - 16) return refused("logic file is truncated");
    if (id != build_id) return refused("logic file belongs to another build (" + std::string(id) + ", expected " + std::string(build_id) + ")");
    std::vector<std::byte> plain(size);
    if (crypto_aead_unlock(reinterpret_cast<uint8_t*>(plain.data()), p + at + size, key.data(), p + 13 + id_len, p, at, p + at, size) != 0)
        return refused("the game files do not unlock the logic file (other files, or the logic file is damaged)");
    return plain;
}
}
