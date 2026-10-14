#include "translation/texture_pack.hpp"
#include "psprecomp/sha256.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void text(Bytes &bytes, std::string_view value) { bytes.insert(bytes.end(), value.begin(), value.end()); }
void integer(Bytes &bytes, std::uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
void name(Bytes &bytes, std::string_view value) { integer(bytes, static_cast<std::uint32_t>(value.size()), 2); text(bytes, value); }
const Bytes original{1, 2, 3, 4};
const Bytes pixels{10, 20, 30, 40, 50, 60, 70, 80};
Bytes fixture(unsigned entries = 1, std::string_view source = "PSP_GAME/synthetic.bin") {
    Bytes bytes;
    text(bytes, "SSTEXPK1UCJS10060"); text(bytes, std::string(64, 'a')); text(bytes, "en");
    integer(bytes, 1, 4); name(bytes, source); text(bytes, psprecomp::sha256(original));
    integer(bytes, entries, 4);
    for (unsigned i = 0; i < entries; ++i) {
        name(bytes, "menu.synthetic");
        for (const auto value : {2, 1, 4, 2, 4}) integer(bytes, value, 2);
        text(bytes, psprecomp::sha256(pixels)); integer(bytes, 8, 4);
        text(bytes, "REPLACE!");
    }
    return bytes;
}
}
int main() {
    using sarusaru::translation::TexturePack;
    const auto path = std::filesystem::temp_directory_path() / ("sarusaru-pack-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sstp");
    auto load = [&](const Bytes &bytes, bool bad_source = false) {
        { std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); }
        return TexturePack::load(path, "UCJS10060", std::string(64, 'a'), [&](std::string_view p) {
            require(p == "PSP_GAME/synthetic.bin", "source reader received wrong path");
            return bad_source ? Bytes{9} : original;
        });
    };
    auto rejects = [&](const Bytes &bytes, bool bad_source = false) {
        try { (void)load(bytes, bad_source); } catch (const std::runtime_error &) { return; }
        require(false, "invalid pack accepted");
    };
    const auto pack = load(fixture());
    require(pack.size() == 1 && pack.locale() == "en", "pack metadata missing");
    sarusaru::gpu::TextureState texture;
    texture.width = 4; texture.height = 2; texture.buffer_width = 4; texture.address = 0x08810000;
    const std::vector<std::uint32_t> native{0x281E140Au, 0x50463C32u, 0x11223344u, 0xAABBCCDDu, 5, 6, 7, 8};
    auto actual = native;
    require(pack.apply(texture, actual), "valid texture not replaced");
    require(actual[0] == 0x4C504552u && actual[1] == 0x21454341u, "replacement byte order wrong");
    require(actual[2] == native[2] && actual[4] == native[4], "padding changed");
    texture.address += 0x10000; actual = native; actual[7] = 999;
    require(pack.apply(texture, actual), "identity depends on address or padding");
    actual = native; actual[0] ^= 1;
    const auto changed = actual;
    require(!pack.apply(texture, actual) && actual == changed, "nonmatching pixels did not retain Japanese");
    actual = native; texture.buffer_width = 3;
    require(!pack.apply(texture, actual), "wrong stride accepted");
    actual.resize(1); require(!pack.apply(texture, actual), "short image accepted");
    auto bytes = fixture();
    for (const auto length : {0u, 7u, 80u, static_cast<unsigned>(bytes.size() - 1)}) rejects(Bytes(bytes.begin(), bytes.begin() + length));
    bytes[0] = 'X'; rejects(bytes);
    bytes = fixture(); bytes[17] = 'b'; rejects(bytes);
    bytes = fixture(); bytes[81] = 'j'; rejects(bytes);
    bytes = fixture(); bytes.push_back(0); rejects(bytes);
    rejects(fixture(), true); rejects(fixture(2)); rejects(fixture(0));
    rejects(fixture(1, "../synthetic.bin")); rejects(fixture(1, "/synthetic.bin"));
    // A partial pack is valid: the caller can retain Japanese for all other assets.
    std::filesystem::remove(path);
    std::cout << "translation pack tests passed\n";
}
