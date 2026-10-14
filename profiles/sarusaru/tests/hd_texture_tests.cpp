#include "gpu/texture_hash.hpp"
#include "textures/hd_pack.hpp"
#include "textures/texture_dump.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void rejects_manifest(std::string_view text) {
    try { (void)sarusaru::textures::parse_hd_manifest(text); }
    catch (const std::runtime_error &) { return; }
    require(false, "malformed manifest accepted");
}
sarusaru::gpu::TextureState texture(std::uint16_t width, std::uint16_t height, std::uint32_t buffer_width) {
    sarusaru::gpu::TextureState state;
    state.width = width;
    state.height = height;
    state.buffer_width = buffer_width;
    return state;
}
}

int main() {
    using namespace sarusaru;
    // Identity: the logical rectangle only, and the shape counts.
    std::vector<std::uint32_t> texels(8 * 4);
    for (std::size_t i = 0; i < texels.size(); ++i) texels[i] = 0xFF000000u | static_cast<std::uint32_t>(i * 0x010203u);
    const auto full = gpu::identify_texture(texture(8, 4, 8), texels);
    require(full.width == 8 && full.height == 4 && full.hash.size() == 32, "full texture identity");
    require(gpu::identify_texture(texture(8, 4, 8), texels).hash == full.hash, "identity not deterministic");
    const auto narrow = gpu::identify_texture(texture(8, 4, 4), texels);
    require(narrow.width == 4 && narrow.hash != full.hash, "row stride not limiting the logical width");
    auto padded = texels;
    for (std::size_t y = 0; y < 4; ++y) for (std::size_t x = 4; x < 8; ++x) padded[y * 8 + x] = 0x12345678u;
    require(gpu::identify_texture(texture(8, 4, 4), padded).hash == narrow.hash, "columns past the stride changed the identity");
    require(gpu::identify_texture(texture(4, 8, 4), texels).hash != narrow.hash, "same bytes in another shape collided");
    require(gpu::identify_texture(texture(8, 8, 8), texels).hash.empty(), "short texel buffer identified");

    // Manifest.
    const auto manifest = textures::parse_hd_manifest("# comment\nformat = 1\ndisc = ULJS00001\nscale = 4\n");
    require(manifest.disc_id == "ULJS00001" && manifest.scale == 4, "manifest not parsed");
    const auto reparsed = textures::parse_hd_manifest(textures::serialize_hd_manifest(manifest));
    require(reparsed.disc_id == manifest.disc_id && reparsed.scale == manifest.scale, "manifest does not round-trip");
    rejects_manifest("format = 2\ndisc = ULJS00001\nscale = 4\n");
    rejects_manifest("format = 1\ndisc = ULJS00001\nscale = 9\n");
    rejects_manifest("format = 1\ndisc = ULJS00001\nscale = 1\n");
    rejects_manifest("format = 1\ndisc = SHORT\nscale = 4\n");
    rejects_manifest("format = 1\ndisc = ULJS00001\n");
    rejects_manifest("format = 1\ndisc = ULJS00001\nscale = 4\nscale = 2\n");
    rejects_manifest("format = 1\ndisc = ULJS00001\nscale = 4\nmodel = x\n");

    const auto directory = std::filesystem::temp_directory_path() /
        ("sarusaru-hd-texture-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    // Dump: once per identity, kept across runs, logical rectangle only.
    const auto dump_directory = directory / "dump";
    {
        textures::TextureDump dump(dump_directory);
        dump.record(texture(8, 4, 4), narrow, texels);
        dump.record(texture(8, 4, 4), narrow, padded);
        dump.record(texture(8, 4, 8), full, texels);
        require(dump.written() == 2, "dump did not write each texture once");
    }
    {
        textures::TextureDump again(dump_directory);
        require(again.known() == 2, "dump index not read back");
        again.record(texture(8, 4, 8), full, texels);
        require(again.written() == 0, "dump rewrote a known texture");
    }
    std::ifstream index(dump_directory / textures::TextureDump::kIndexName);
    std::string header;
    std::getline(index, header);
    require(header == textures::TextureDump::kIndexHeader, "dump index header");

    // Pack: a 2x image for `narrow`, made by doubling the dumped one, and a
    // wrongly sized one for `full`.
    const auto pack_directory = directory / "pack";
    std::filesystem::create_directories(pack_directory);
    { std::ofstream out(pack_directory / textures::HdTexturePack::kManifestName);
      out << textures::serialize_hd_manifest({"ULJS00001", 2}); }
    std::vector<std::uint32_t> doubled(8 * 8);
    for (std::size_t y = 0; y < 8; ++y) for (std::size_t x = 0; x < 8; ++x) doubled[y * 8 + x] = texels[(y / 2) * 8 + x / 2];
    require(textures::write_png(pack_directory / (narrow.hash + ".png"), doubled, 8, 8, 8), "write HD image");
    require(textures::write_png(pack_directory / (full.hash + ".png"), texels, 8, 4, 8), "write wrong-size image");
    { std::ofstream out(pack_directory / "notes.png"); out << "not a texture"; }

    try { (void)textures::HdTexturePack::load(pack_directory, "ULJS00002"); require(false, "pack for another disc accepted"); }
    catch (const std::runtime_error &) {}
    try { (void)textures::HdTexturePack::load(directory / "missing", "ULJS00001"); require(false, "missing pack accepted"); }
    catch (const std::runtime_error &) {}
    const auto pack = textures::HdTexturePack::load(pack_directory, "ULJS00001");
    require(pack.size() == 2 && pack.scale() == 2, "pack index");
    const auto found = pack.find(narrow);
    require(found && *found == doubled, "HD image not returned as written");
    require(!pack.find(full), "wrongly sized HD image accepted");
    require(!pack.find(gpu::identify_texture(texture(4, 8, 4), texels)), "unknown texture found");

    std::filesystem::remove_all(directory);
    std::cout << "HD texture tests passed\n";
}
