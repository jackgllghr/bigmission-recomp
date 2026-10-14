// HD texture recipes: disc formats, recipe parsing, upscale plans, image
// planes, and making a pack from an invented disc with a stand-in upscaler.
// Every picture here is invented; no game data is involved.
#include "install/texture_build.hpp"
#include "textures/disc_textures.hpp"
#include "textures/hd_pack.hpp"
#include "textures/image.hpp"
#include "textures/recipe.hpp"
#include "textures/texture_dump.hpp"
#include "umd/disc_image.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace sarusaru;
using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template <typename F> void throws(F &&f, const char *message) {
    try { f(); } catch (const std::runtime_error &) { return; }
    require(false, message);
}
void put32(Bytes &out, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4u; ++i) out[at + i] = static_cast<std::uint8_t>(value >> (8u * i));
}

// All literals: a flag byte of ones before every eight bytes.
Bytes lzs(const Bytes &data) {
    Bytes out(4);
    put32(out, 0, static_cast<std::uint32_t>(data.size()));
    for (std::size_t i = 0; i < data.size(); i += 8u) {
        out.push_back(0xFF);
        for (std::size_t j = i; j < std::min(data.size(), i + 8u); ++j) out.push_back(data[j]);
    }
    return out;
}

Bytes pac(const std::vector<std::pair<std::string, Bytes>> &members) {
    Bytes out(members.size() * 48u);
    for (std::size_t i = 0; i < members.size(); ++i) {
        const auto offset = static_cast<std::uint32_t>(out.size());
        put32(out, i * 48u + 4u, offset);
        put32(out, i * 48u + 8u, static_cast<std::uint32_t>(members[i].second.size()));
        put32(out, i * 48u + 12u, static_cast<std::uint32_t>(i));
        std::copy(members[i].first.begin(), members[i].first.end(), out.begin() + static_cast<std::ptrdiff_t>(i * 48u + 16u));
        out.insert(out.end(), members[i].second.begin(), members[i].second.end());
    }
    out[0] = static_cast<std::uint8_t>(members.size());
    out[1] = 0;
    return out;
}

// An indexed TIM2 whose texel (x, y) is palette entry index(x, y).
Bytes tim2(unsigned bits, std::uint16_t width, std::uint16_t height, const std::vector<std::uint32_t> &palette,
           std::uint32_t (*index)(std::uint32_t, std::uint32_t), bool csm1 = true) {
    const std::uint32_t row = width * bits / 8u, image = row * height, palette_bytes = static_cast<std::uint32_t>(palette.size()) * 4u;
    Bytes out(16u + 48u + image + palette_bytes);
    out[0] = 'T'; out[1] = 'I'; out[2] = 'M'; out[3] = '2'; out[4] = 4; out[6] = 1;
    put32(out, 16, 48u + image + palette_bytes);
    put32(out, 20, palette_bytes);
    put32(out, 24, image);
    out[28] = 48;
    out[30] = static_cast<std::uint8_t>(palette.size()); out[31] = static_cast<std::uint8_t>(palette.size() >> 8u);
    out[33] = 1;
    out[34] = csm1 ? 3 : 0x43;
    out[35] = bits == 4u ? 4 : 5;
    out[36] = static_cast<std::uint8_t>(width); out[37] = static_cast<std::uint8_t>(width >> 8u);
    out[38] = static_cast<std::uint8_t>(height); out[39] = static_cast<std::uint8_t>(height >> 8u);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t byte_x = x * bits / 8u;
            const std::uint32_t at = 64u + ((y / 8u) * (row / 16u) + byte_x / 16u) * 128u + (y % 8u) * 16u + byte_x % 16u;
            std::uint32_t value = index(x, y);
            if (bits == 4u) {
                out[at] = static_cast<std::uint8_t>(out[at] | (value & 15u) << ((x % 2u) * 4u));
            } else {
                // CSM1 swaps bits 3 and 4 of the stored index.
                if (csm1) value = (value & ~24u) | ((value & 8u) << 1u) | ((value & 16u) >> 1u);
                out[at] = static_cast<std::uint8_t>(value);
            }
        }
    for (std::size_t i = 0; i < palette.size(); ++i) put32(out, 64u + image + i * 4u, palette[i]);
    return out;
}

// An ISO9660 image of `files` (slash paths) in nested directories.
std::vector<std::uint8_t> iso(const std::map<std::string, Bytes> &files) {
    constexpr std::uint32_t kSector = umd::kSectorSize;
    struct Node { std::map<std::string, Node> dirs; std::map<std::string, const Bytes *> files; std::uint32_t sector{}; };
    Node root;
    for (const auto &[path, data] : files) {
        Node *node = &root;
        std::string rest = path;
        for (auto slash = rest.find('/'); slash != std::string::npos; slash = rest.find('/')) {
            node = &node->dirs[rest.substr(0, slash)];
            rest = rest.substr(slash + 1u);
        }
        node->files[rest] = &data;
    }
    std::uint32_t next = 18u;
    std::map<const Bytes *, std::uint32_t> file_sectors;
    std::function<void(Node &)> place = [&](Node &node) {
        node.sector = next++;
        for (auto &[name, child] : node.dirs) place(child);
    };
    place(root);
    for (const auto &[path, data] : files) {
        file_sectors[&data] = next;
        next += static_cast<std::uint32_t>((data.size() + kSector - 1u) / kSector);
    }
    std::vector<std::uint8_t> image(std::size_t{next} * kSector);
    const auto both = [&](std::size_t at, std::uint32_t value, std::size_t width) {
        for (std::size_t i = 0; i < width; ++i) {
            image[at + i] = static_cast<std::uint8_t>(value >> (8u * i));
            image[at + 2u * width - 1u - i] = static_cast<std::uint8_t>(value >> (8u * i));
        }
    };
    const auto record = [&](std::size_t at, std::string_view name, std::uint32_t sector, std::uint32_t size, bool directory) {
        const auto length = static_cast<std::uint8_t>((33u + name.size() + 1u) & ~1u);
        image[at] = length;
        both(at + 2u, sector, 4u);
        both(at + 10u, size, 4u);
        image[at + 25u] = directory ? 2u : 0u;
        both(at + 28u, 1u, 2u);
        image[at + 32u] = static_cast<std::uint8_t>(name.size());
        std::copy(name.begin(), name.end(), image.begin() + static_cast<std::ptrdiff_t>(at + 33u));
        return at + length;
    };
    const std::size_t pvd = 16u * kSector;
    image[pvd] = 1;
    std::copy_n("CD001", 5, image.begin() + static_cast<std::ptrdiff_t>(pvd + 1u));
    image[pvd + 6u] = 1;
    both(pvd + 80u, next, 4u);
    both(pvd + 128u, kSector, 2u);
    record(pvd + 156u, std::string_view("\0", 1), root.sector, kSector, true);
    std::function<void(Node &, std::uint32_t)> write = [&](Node &node, std::uint32_t parent) {
        auto at = record(std::size_t{node.sector} * kSector, std::string_view("\0", 1), node.sector, kSector, true);
        at = record(at, std::string_view("\1", 1), parent, kSector, true);
        for (auto &[name, child] : node.dirs) at = record(at, name, child.sector, kSector, true);
        for (const auto &[name, data] : node.files)
            at = record(at, name + ";1", file_sectors.at(data), static_cast<std::uint32_t>(data->size()), false);
        for (auto &[name, child] : node.dirs) write(child, node.sector);
    };
    write(root, root.sector);
    for (const auto &[path, data] : files)
        std::copy(data.begin(), data.end(), image.begin() + static_cast<std::ptrdiff_t>(std::size_t{file_sectors.at(&data)} * kSector));
    return image;
}

std::uint32_t pattern4(std::uint32_t x, std::uint32_t y) { return (x + 3u * y) % 16u; }
std::uint32_t pattern8(std::uint32_t x, std::uint32_t y) { return (x * 7u + y * 13u) % 256u; }

std::vector<std::uint32_t> palette(std::size_t count) {
    std::vector<std::uint32_t> result(count);
    for (std::size_t i = 0; i < count; ++i)
        result[i] = static_cast<std::uint32_t>((i * 37u) & 0xFFu) | static_cast<std::uint32_t>((i * 11u) & 0xFFu) << 8u |
                    static_cast<std::uint32_t>((255u - i) & 0xFFu) << 16u | static_cast<std::uint32_t>(i % 3u == 0u ? 0u : 255u) << 24u;
    return result;
}

std::string read(const std::filesystem::path &path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

int main() {
    using namespace sarusaru::textures;
    // LZS and PAC.
    const Bytes payload = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    require(decode_lzs(lzs(payload)) == payload, "LZS literals");
    {
        // A back-reference: three literals, then 3 bytes copied from ring position 4078.
        Bytes data = {6, 0, 0, 0, 0x07, 'a', 'b', 'c', 0xEE, 0xF0};
        const auto out = decode_lzs(data);
        require(std::string(out.begin(), out.end()) == "abcabc", "LZS back-reference");
    }
    throws([&] { (void)decode_lzs(Bytes{4, 0, 0, 0, 0xFF, 1}); }, "truncated LZS accepted");
    const auto archive = pac({{"a.tm2", {1, 2, 3}}, {"b.bin", {4, 5}}});
    const auto members = pac_members(archive);
    require(members.size() == 2 && members.at("b.bin").size() == 2 && members.at("a.tm2")[2] == 3, "PAC members");
    throws([&] { auto bad = archive; put32(bad, 48u + 4u, 96u - 1u); (void)pac_members(bad); }, "overlapping PAC accepted");

    // TIM2: 4-bit, 8-bit with CSM1 and CSM2 palettes.
    const auto palette16 = palette(16), palette256 = palette(256);
    const auto four = decode_tim2(tim2(4, 32, 8, palette16, pattern4));
    require(four && four->width == 32 && four->height == 8, "4-bit TIM2 decoded");
    for (std::uint32_t y = 0; y < 8; ++y) for (std::uint32_t x = 0; x < 32; ++x)
        require(four->texels[y * 32 + x] == palette16[pattern4(x, y)], "4-bit TIM2 texel");
    for (const bool csm1 : {true, false}) {
        const auto eight = decode_tim2(tim2(8, 16, 16, palette256, pattern8, csm1));
        require(eight && eight->width == 16, "8-bit TIM2 decoded");
        for (std::uint32_t y = 0; y < 16; ++y) for (std::uint32_t x = 0; x < 16; ++x)
            require(eight->texels[y * 16 + x] == palette256[pattern8(x, y)], "8-bit TIM2 texel");
    }
    {
        auto direct = tim2(4, 32, 8, palette16, pattern4);
        direct[35] = 3;  // 32-bit direct colour: not decoded
        require(!decode_tim2(direct), "direct-colour TIM2 decoded");
        auto truncated = tim2(4, 32, 8, palette16, pattern4);
        truncated.resize(truncated.size() - 4u);
        throws([&] { (void)decode_tim2(truncated); }, "truncated TIM2 accepted");
    }

    // Locators.
    const auto locator = parse_locator("PSP_GAME/USRDIR/data/tex.lzs!lzs|inner.pac|a.tm2#1f0");
    require(locator.path == "PSP_GAME/USRDIR/data/tex.lzs" && locator.steps.size() == 3 && locator.offset == 0x1f0 &&
            locator.steps[1].member == "inner.pac", "locator parsed");
    require(format_locator(locator) == "PSP_GAME/USRDIR/data/tex.lzs!lzs|inner.pac|a.tm2#1f0", "locator round trip");
    for (const char *bad : {"PSP_GAME/a.tm2", "/abs/a.tm2#0", "PSP_GAME/../a#0", "a!zip#0", "a|#0", "a#zz"})
        throws([&] { (void)parse_locator(bad); }, "malformed locator accepted");

    // Recipes and plans.
    const std::string text =
        "# invented\nsarusaru-texture-recipe 1\ndisc TEST00001\ndefaults model=realesrgan-x4plus-anime scale=8 alpha=model\n"
        "scan PSP_GAME/USRDIR/data\n"
        "texture 0123456789abcdef0123456789abcdef PSP_GAME/x.lzs!lzs|a.tm2#0 alpha=opaque edge=wrap\n"
        "texture fedcba9876543210fedcba9876543210 - model=nearest\n"
        "skip 00000000000000000000000000000000\n";
    const auto recipe = parse_recipe(text);
    require(recipe.disc_id == "TEST00001" && recipe.scans.size() == 1 && recipe.textures.size() == 2 && recipe.skip.size() == 1,
            "recipe parsed");
    const auto wrapped = recipe.options("0123456789abcdef0123456789abcdef");
    require(wrapped.alpha == AlphaMode::Opaque && wrapped.edge == Edge::Wrap && wrapped.model == Model::X4PlusAnime &&
            wrapped.max_scale == 8, "texture options over defaults");
    require(recipe.options("11111111111111111111111111111111").alpha == AlphaMode::Model, "defaults for unlisted");
    require(parse_recipe(serialize_recipe(recipe)).textures == recipe.textures, "recipe round trip");
    for (const char *bad : {"sarusaru-texture-recipe 2\ndisc TEST00001\n", "sarusaru-texture-recipe 1\n",
                            "sarusaru-texture-recipe 1\ndisc TEST00001\ntexture 0123 - \n",
                            "sarusaru-texture-recipe 1\ndisc TEST00001\ndefaults model=waifu\n",
                            "sarusaru-texture-recipe 1\ndisc TEST00001\ndefaults scale=3\n",
                            "sarusaru-texture-recipe 1\ndisc TEST00001\npixels 1 2 3\n",
                            "sarusaru-texture-recipe 1\ndisc TEST00001\ntexture fedcba9876543210fedcba9876543210 -\n"
                            "skip fedcba9876543210fedcba9876543210\n"})
        throws([&] { (void)parse_recipe(bad); }, "malformed recipe accepted");
    require(plan_upscale({Model::X4PlusAnime, 8, AlphaMode::Model, Edge::Clamp}, 4) ==
                UpscalePlan{{{"realesrgan-x4plus-anime", 4}}, 1}, "4x plan");
    require(plan_upscale({Model::X4Plus, 8, AlphaMode::Model, Edge::Clamp}, 8) ==
                UpscalePlan{{{"realesrgan-x4plus", 4}, {"realesr-animevideov3", 2}}, 1}, "8x plan");
    require(plan_upscale({Model::X4Plus, 4, AlphaMode::Model, Edge::Clamp}, 8) ==
                UpscalePlan{{{"realesrgan-x4plus", 4}}, 2}, "capped plan finishes with nearest");
    require(plan_upscale({Model::X4Plus, 2, AlphaMode::Model, Edge::Clamp}, 4) ==
                UpscalePlan{{{"realesr-animevideov3", 2}}, 2}, "2x cap");
    require(plan_upscale({Model::Nearest, 8, AlphaMode::Model, Edge::Clamp}, 8) == UpscalePlan{{}, 8}, "nearest plan");

    // Planes, padding and resizing.
    Image small{2, 2, {0x00112233u, 0xFF445566u, 0x80778899u, 0x10AABBCCu}};
    require(combine(colour_plane(small), alpha_plane(small)) == small, "planes split and rejoin straight");
    require(colour_plane(small).at(0, 0) == 0xFF112233u, "colour kept under zero alpha");
    const auto wrap = pad(small, 1, Edge::Wrap), clamp = pad(small, 1, Edge::Clamp);
    require(wrap.width == 4 && wrap.at(0, 0) == small.at(1, 1) && clamp.at(0, 0) == small.at(0, 0), "padding");
    require(crop(wrap, 1, 1, 2, 2) == small, "crop undoes padding");
    require(resize_nearest(small, 2).at(3, 0) == small.at(1, 0), "nearest");
    const Image flat{2, 1, {0xFF000000u, 0xFF0000FFu}};
    const auto smooth = resize_linear(flat, 4, Edge::Clamp);
    require((smooth.at(0, 0) & 0xFFu) == 0u && (smooth.at(7, 0) & 0xFFu) == 255u &&
            (smooth.at(3, 0) & 0xFFu) > 0u && (smooth.at(3, 0) & 0xFFu) < 255u, "linear");

    // A pack from an invented disc: one texture named by locator, one found by
    // scanning, one collected while playing, one skipped.
    const auto base = std::filesystem::temp_directory_path() /
        ("sarusaru-recipe-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(base);
    const auto named_tim2 = tim2(4, 32, 8, palette16, pattern4), scanned_tim2 = tim2(8, 16, 16, palette256, pattern8);
    const auto other_tim2 = tim2(4, 32, 16, palette16, pattern4);
    Bytes loose(100, 0x55);
    loose.insert(loose.end(), other_tim2.begin(), other_tim2.end());
    const std::map<std::string, Bytes> files = {
        {"PSP_GAME/USRDIR/data/tex.lzs", lzs(pac({{"named.tm2", named_tim2}, {"scanned.tm2", scanned_tim2}}))},
        {"PSP_GAME/USRDIR/data/loose.dat", loose},
        {"PSP_GAME/USRDIR/music.at3", Bytes(200, 1)},
    };
    {
        const auto image = iso(files);
        std::ofstream(base / "disc.iso", std::ios::binary).write(reinterpret_cast<const char *>(image.data()),
                                                                 static_cast<std::streamsize>(image.size()));
    }
    const auto named = *decode_tim2(named_tim2), scanned = *decode_tim2(scanned_tim2), other = *decode_tim2(other_tim2);
    {
        umd::DiscImage disc(base / "disc.iso");
        std::map<std::string, std::string> found;
        scan_disc(disc, "PSP_GAME/USRDIR", [&](const SourceLocator &where, Picture &&picture) {
            found[picture.identity().hash] = format_locator(where);
            return true;
        });
        require(found.size() == 3 && found.at(named.identity().hash) == "PSP_GAME/USRDIR/data/tex.lzs!lzs|named.tm2#0" &&
                found.at(other.identity().hash) == "PSP_GAME/USRDIR/data/loose.dat#64", "disc scan");
        require(read_picture(disc, parse_locator("PSP_GAME/USRDIR/data/tex.lzs!lzs|scanned.tm2#0")).texels == scanned.texels,
                "picture read by locator");
    }
    // The collected texture: invented pixels the game would have dumped.
    Image played{8, 8, {}};
    for (std::uint32_t i = 0; i < 64; ++i) played.texels.push_back(0xFF000000u | i * 0x030507u);
    gpu::TextureState state;
    state.width = 8; state.height = 8; state.buffer_width = 8;
    const auto played_hash = gpu::identify_texture(state, played.texels).hash;
    const std::string recipe_text =
        "sarusaru-texture-recipe 1\ndisc TEST00001\ndefaults model=realesrgan-x4plus-anime scale=8 alpha=model\n"
        "scan PSP_GAME/USRDIR/data\n"
        "texture " + named.identity().hash + " PSP_GAME/USRDIR/data/tex.lzs!lzs|named.tm2#0\n"
        "texture " + played_hash + " - alpha=opaque\n"
        "texture 0123456789abcdef0123456789abcdef PSP_GAME/USRDIR/data/tex.lzs!lzs|scanned.tm2#0\n"
        "skip " + other.identity().hash + "\n";
    const auto upscaler = std::filesystem::path(SARUSARU_FAKE_UPSCALER);
    install::PackInputs inputs{recipe_text, "TEST00001", base / "disc.iso", upscaler, base / "models", 4, base / "pack4"};
    install::ProcessGroup processes;
    install::TextureProgress last;
    install::build_texture_pack(inputs, processes, [&](const install::TextureProgress &p) { last = p; });
    // A listed texture its locator does not give is collected while playing, like one with no locator.
    require(last.step == install::TextureProgress::Step::Done && last.textures == 2 && last.waiting == 2,
            "pack made: named and scanned, two waiting");
    const auto wanted = read(base / "pack4" / "wanted.txt");
    require(wanted.find(played_hash + "\n") != std::string::npos &&
            wanted.find("0123456789abcdef0123456789abcdef\n") != std::string::npos, "wanted list");
    require(!std::filesystem::exists(base / "pack4" / (other.identity().hash + ".png")), "skipped texture made");
    require(!std::filesystem::exists(base / "pack4" / "0123456789abcdef0123456789abcdef.png"),
            "locator naming another picture trusted");
    {
        const auto pack = HdTexturePack::load(base / "pack4", "TEST00001");
        require(pack.scale() == 4 && pack.size() == 2, "pack loads");
        // Nearest stand-in: colour and alpha come back exactly, colour under zero alpha included.
        require(*pack.find(named.identity()) == resize_nearest(Image{32, 8, named.texels}, 4).texels, "named texture exact");
        require(*pack.find(scanned.identity()) == resize_nearest(Image{16, 16, scanned.texels}, 4).texels, "scanned texture exact");
        require(pack.wanted() && pack.wanted()->contains(played_hash), "pack lists what to collect");
    }
    require(!std::filesystem::exists(base / "pack4.work"), "work cleaned");

    // The game collects the waiting texture; the next build adds it.
    {
        TextureDump dump(base / "pack4" / "collected", std::unordered_set<std::string>{played_hash});
        gpu::TextureIdentity id{8, 8, played_hash};
        dump.record(state, id, played.texels);
        dump.record(state, named.identity(), named.texels);
        require(dump.written() == 1, "only wanted textures collected");
    }
    require(install::texture_pack_state(base / "pack4") == install::TexturePackState::Collected, "collected state");
    install::build_texture_pack(inputs, processes, [&](const install::TextureProgress &p) { last = p; });
    require(last.textures == 3 && last.waiting == 1 && read(base / "pack4" / "wanted.txt") == "0123456789abcdef0123456789abcdef\n",
            "collected texture made");
    require(install::texture_pack_state(base / "pack4") == install::TexturePackState::Ready, "ready state");
    require(*HdTexturePack::load(base / "pack4", "TEST00001").find({8, 8, played_hash}) == resize_nearest(played, 4).texels,
            "collected texture exact");

    // 8x: the model's 4x then the 2x model; the wrong disc is refused.
    inputs.scale = 8;
    inputs.pack = base / "pack8";
    install::build_texture_pack(inputs, processes, [&](const install::TextureProgress &) {});
    require(*HdTexturePack::load(base / "pack8", "TEST00001").find(named.identity()) ==
                resize_nearest(Image{32, 8, named.texels}, 8).texels, "8x texture exact");
    inputs.disc_id = "TEST00002";
    throws([&] { install::build_texture_pack(inputs, processes, [](const install::TextureProgress &) {}); },
           "recipe for another disc used");

    std::filesystem::remove_all(base);
    std::cout << "Texture recipe tests passed\n";
}
