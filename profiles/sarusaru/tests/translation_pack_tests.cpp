#include "translation/texture_pack.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
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
Bytes version2() {
    auto bytes = fixture(); bytes[7] = '2';
    Bytes region; integer(region, 1, 2);
    for (const auto value : {1, 0, 1, 1}) integer(region, value, 2);
    bytes.insert(bytes.end() - 12, region.begin(), region.end());
    return bytes;
}
void string_record(Bytes &bytes, std::string_view id, std::uint32_t offset,
                   const Bytes &source, std::string_view replacement, bool bad_hash = false) {
    name(bytes, id); integer(bytes, offset, 4); integer(bytes, source.size(), 4);
    text(bytes, bad_hash ? std::string(64, '0') : psprecomp::sha256(source));
    integer(bytes, replacement.size(), 2); text(bytes, replacement);
}
psprecomp::Elf32Image string_elf(const Bytes &first, const Bytes &second) {
    Bytes bytes(640, 0);
    const auto put = [&](std::size_t offset, std::uint32_t value, unsigned size = 4) {
        for (unsigned i = 0; i < size; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F'; bytes[4] = bytes[5] = bytes[6] = 1;
    put(16, 2, 2); put(18, 8, 2); put(20, 1); put(24, 0x08801000); put(28, 52); put(32, 384);
    put(40, 52, 2); put(42, 32, 2); put(44, 1, 2); put(46, 40, 2); put(48, 4, 2); put(50, 2, 2);
    put(52, 1); put(56, 256); put(60, 0x08801000); put(68, 104); put(72, 104); put(76, 4); put(80, 16);
    std::copy(first.begin(), first.end(), bytes.begin() + 256);
    std::copy(second.begin(), second.end(), bytes.begin() + 288);
    const char names[] = "\0.rodata\0.shstrtab\0.data\0";
    std::copy(std::begin(names), std::end(names), bytes.begin() + 320);
    put(424, 1); put(428, 1); put(432, 2); put(436, 0x08801000); put(440, 256); put(444, 64); put(456, 16);
    put(464, 9); put(468, 3); put(480, 320); put(484, sizeof(names)); put(496, 1);
    put(352, 0x08801000); put(356, 0x08801020);
    put(504, 19); put(508, 1); put(512, 3); put(516, 0x08801060); put(520, 352); put(524, 8); put(536, 4);
    return psprecomp::Elf32Image::from_bytes(std::move(bytes));
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
    // The gameplay catalog includes more than 256 names and UI strings.
    auto many_strings = version2();
    integer(many_strings, 400, 4);
    for (unsigned i = 0; i < 400; ++i)
        string_record(many_strings, "monkey.synthetic." + std::to_string(i), 1024 + i * 2, Bytes{'Z', 0}, "A");
    integer(many_strings, 0, 2);
    require(load(many_strings).string_count() == 400, "expanded monkey/UI catalog rejected");
    auto oversized_strings = version2(); integer(oversized_strings, 1025, 4); rejects(oversized_strings);

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

    // V2 masks replace only authored regions; icons and other labels survive.
    auto v2 = version2(); integer(v2, 0, 4); integer(v2, 0, 2);
    auto masked = load(v2); texture.buffer_width = 4; actual = native;
    require(masked.apply(texture, actual) && actual[0] == native[0] && actual[1] == 0x21454341u,
            "partial atlas replacement damaged an unselected region");

    // V5 retains the native replacement and adds subpixel lettering. The two
    // colors occupy halves of one guest texel, proving this is real HD detail.
    auto hd_fixture = [&](unsigned scale = 8, std::string_view id = "menu.synthetic") {
        auto result = version2(); result[7] = '5';
        integer(result, 0, 4); integer(result, 0, 2); integer(result, 0, 2);
        integer(result, 1, 2); name(result, id); integer(result, scale, 2);
        Bytes runs;
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 16; ++x) {
                integer(runs, 1, 2);
                integer(runs, x < 8 ? 0xffffffffu : x < 12 ? 0xff0000ffu : 0xff00ff00u, 4);
            }
        integer(result, runs.size(), 4); result.insert(result.end(), runs.begin(), runs.end());
        return result;
    };
    auto hd_pack = load(hd_fixture());
    for (unsigned requested = 1; requested <= 8; ++requested) {
        actual = native;
        const auto scale = hd_pack.apply_scaled(texture, actual, requested);
        const auto expected_scale = requested == 1 ? 1u : requested <= 2 ? 2u : requested <= 4 ? 4u : 8u;
        require(scale == expected_scale && actual.size() == native.size() * scale * scale,
                "HD replacement did not follow render resolution");
        for (unsigned y = 0; y < texture.height * scale; ++y)
            for (unsigned x = 0; x < texture.width * scale; ++x) {
                const bool replaced = y < scale && x >= scale && x < 2 * scale;
                const auto expected = !replaced ? native[(y / scale) * texture.width + x / scale]
                    : scale == 1 ? 0x21454341u : x < scale + scale / 2 ? 0xff0000ffu : 0xff00ff00u;
                require(actual[y * texture.width * scale + x] == expected,
                        "HD text lost subpixel detail or changed icons/padding");
            }
    }
    actual = native; actual[0] ^= 1; const auto unmatched_hd = actual;
    require(hd_pack.apply_scaled(texture, actual, 8) == 0 && actual == unmatched_hd,
            "HD replacement skipped source verification");
    actual = native;
    require(hd_pack.apply_scaled(texture, actual, 9) == 0 && actual == native, "unbounded HD scale accepted");
    rejects(hd_fixture(0)); rejects(hd_fixture(8, "unknown"));
    auto bad_hd = hd_fixture(); bad_hd[bad_hd.size() - 6] = 0; rejects(bad_hd);
    bad_hd = hd_fixture(); bad_hd[bad_hd.size() - 6] = 2; rejects(bad_hd);
    bad_hd = hd_fixture(); bad_hd.pop_back(); rejects(bad_hd);

    // V6 carries the movies' subtitles as SubRip text, by movie name.
    auto subtitle_fixture = [&](std::vector<std::pair<std::string, std::string>> movies) {
        auto result = hd_fixture(); result[7] = '6';
        integer(result, movies.size(), 2);
        for (const auto &[key, srt] : movies) { name(result, key); integer(result, srt.size(), 4); text(result, srt); }
        return result;
    };
    const std::string srt = "1\n00:00:01,000 --> 00:00:02,000\nHello\n";
    const auto subtitled = load(subtitle_fixture({{"m01", srt}, {"opening", srt}}));
    require(subtitled.subtitles().size() == 2 && subtitled.subtitles().at("m01") == srt, "pack subtitles not read");
    require(subtitled.size() == 1 && load(subtitle_fixture({})).subtitles().empty(), "subtitle section disturbed the pack");
    require(load(hd_fixture()).subtitles().empty(), "V5 pack gained subtitles");
    rejects(subtitle_fixture({{"m01", srt}, {"m01", srt}}));
    rejects(subtitle_fixture({{"M01", srt}})); rejects(subtitle_fixture({{"m/01", srt}}));
    rejects(subtitle_fixture({{"m01", ""}})); rejects(subtitle_fixture({{"m01", std::string("a\0b", 3)}}));
    rejects(subtitle_fixture({{"m01", std::string(256u * 1024u + 1u, 'a')}}));
    auto short_subtitles = subtitle_fixture({{"m01", srt}}); short_subtitles.pop_back(); rejects(short_subtitles);
    auto future = hd_fixture(); future[7] = '7'; rejects(future);

    Bytes first; text(first, "Value %s is ready."); first.push_back(0);
    Bytes second; text(second, "Second original string."); second.push_back(0);
    auto elf = string_elf(first, second);
    auto string_pack = [&](bool bad_second, std::string_view replacement = "Ready %s.") {
        auto result = version2(); integer(result, 2, 4);
        string_record(result, "dialog.first", 256, first, replacement);
        string_record(result, "dialog.second", 288, second, "Second.", bad_second);
        integer(result, 0, 2); return result;
    };
    psprecomp::GuestMemory memory; elf.load_into(memory);
    try { load(string_pack(true)).apply_strings(memory, elf, 0); require(false, "bad source accepted"); }
    catch (const std::runtime_error &) {}
    require(memory.load8(0x08801000) == 'V', "failed string validation left a partial write");
    try { load(string_pack(false, "Ready %d.")).apply_strings(memory, elf, 0); require(false, "placeholder type changed"); }
    catch (const std::runtime_error &) {}
    auto strings = load(string_pack(false)); strings.apply_strings(memory, elf, 0);
    require(memory.load8(0x08801000) == 'R' && memory.load8(0x08801009) == 0 && memory.load8(0x08801020) == 'S',
            "bounded read-only strings not replaced/terminated");
    elf.load_into(memory);
    auto outside = version2(); integer(outside, 1, 4); string_record(outside, "bad", 52, first, "Ready %s."); integer(outside, 0, 2);
    try { load(outside).apply_strings(memory, elf, 0); require(false, "non-rodata patch accepted"); }
    catch (const std::runtime_error &) {}

    // V3 permits verified zero-padded fixed name fields, preserving their tail
    // metadata and neighboring strings. Older V2 retains strict NUL slot rules.
    Bytes padded; text(padded, "Name"); padded.resize(24, 0);
    auto padded_elf = string_elf(padded, second);
    auto padded_pack = [&](const Bytes &source, char version) {
        auto result = version2(); result[7] = version; integer(result, 1, 4);
        string_record(result, "character.synthetic.name", 256, source, "Longer Name");
        integer(result, 0, 2); return result;
    };
    padded_elf.load_into(memory);
    try { load(padded_pack(padded, '2')).apply_strings(memory, padded_elf, 0); require(false, "V2 accepted padded field"); }
    catch (const std::runtime_error &) {}
    load(padded_pack(padded, '3')).apply_strings(memory, padded_elf, 0);
    require(memory.load8(0x08801000) == 'L' && memory.load8(0x0880100B) == 0 &&
            memory.load8(0x08801017) == 0 && memory.load8(0x08801020) == 'S',
            "V3 padded field replacement exceeded its name field");
    auto nonzero_padding = padded; nonzero_padding.back() = 42;
    auto bad_padded_elf = string_elf(nonzero_padding, second); bad_padded_elf.load_into(memory);
    try { load(padded_pack(nonzero_padding, '3')).apply_strings(memory, bad_padded_elf, 0);
          require(false, "nonzero metadata treated as name padding"); }
    catch (const std::runtime_error &) {}
    require(memory.load8(0x08801000) == 'N', "failed padded field validation modified memory");

    // V4 redirects lookup slots into a separately reserved buffer. An escaped
    // glyph with a zero trail is preserved; source messages are never overwritten.
    const Bytes compact_first{0x3f, 0xf0, 0x00, 0x00}; // Cross, glyph 239, terminator
    const Bytes compact_second{0x01, 0x0a, 0x00};
    auto dialogue_elf = string_elf(compact_first, compact_second);
    auto dialogue_record = [&](Bytes &result, std::string_view id, std::uint32_t pointer,
                               std::uint32_t source, const Bytes &raw, const Bytes &replacement, bool bad_hash) {
        name(result, id); integer(result, pointer, 4); integer(result, source, 4); integer(result, raw.size(), 4);
        text(result, bad_hash ? std::string(64, '0') : psprecomp::sha256(raw));
        integer(result, replacement.size(), 4); result.insert(result.end(), replacement.begin(), replacement.end());
    };
    auto dialogue_pack = [&](bool bad_second, const Bytes &replacement = Bytes{0x9f, 0x3f, 0xf0, 0, 0}) {
        auto result = version2(); result[7] = '4'; integer(result, 0, 4); integer(result, 0, 2); integer(result, 2, 2);
        dialogue_record(result, "dialogue.first", 352, 256, compact_first, replacement, false);
        dialogue_record(result, "dialogue.second", 356, 288, compact_second, Bytes{0x9f, 0}, bad_second);
        return result;
    };
    dialogue_elf.load_into(memory);
    unsigned allocations = 0;
    auto reserve = [&](std::uint32_t size) { ++allocations; require(size < 1024, "oversized synthetic allocation"); return 0x08900000u; };
    try { load(dialogue_pack(true)).apply_dialogue(memory, dialogue_elf, 0, reserve); require(false, "changed second dialogue source accepted"); }
    catch (const std::runtime_error &) {}
    require(allocations == 0 && memory.load32(0x08801060) == 0x08801000, "failed dialogue validation left a partial redirect");
    for (const Bytes bad : {Bytes{0x9f, 0}, Bytes{0x3f, 0, 1}, Bytes{0x3f, 0xf0},
                           Bytes{0x3f, 0xff, 0x9f, 0}, Bytes(22, 0x3f)}) {
        try { load(dialogue_pack(false, bad)).apply_dialogue(memory, dialogue_elf, 0, reserve); require(false, "bad compact controls/grammar/layout accepted"); }
        catch (const std::runtime_error &) {}
    }
    require(allocations == 0, "invalid compact payload allocated memory");
    auto messages = load(dialogue_pack(false));
    try { messages.apply_dialogue(memory, dialogue_elf, 0, [](auto) { return 0u; }); require(false, "failed allocation accepted"); }
    catch (const std::runtime_error &) {}
    try { messages.apply_dialogue(memory, dialogue_elf, 0, [](auto) { return 0x08801000u; }); require(false, "destination overlapped executable"); }
    catch (const std::runtime_error &) {}
    require(memory.load32(0x08801060) == 0x08801000, "allocation failure changed dialogue lookup");
    messages.apply_dialogue(memory, dialogue_elf, 0, reserve);
    require(messages.dialogue_count() == 2 && memory.load32(0x08801060) == 0x08900000 &&
            memory.load32(0x08801064) == 0x08900005 && memory.load8(0x08900000) == 0x9f &&
            memory.load8(0x08900003) == 0 && memory.load8(0x08801000) == 0x3f,
            "dialogue redirection damaged embedded zero, original source or pointer offsets");

    // Complete glyph runs translate once, independent of guest addresses.
    Bytes glyph_signature;
    for (const auto value : {0, 0, 0, 16, 16, 0}) integer(glyph_signature, value, 2);
    auto run_bytes = version2(); integer(run_bytes, 0, 4); integer(run_bytes, 1, 2);
    name(run_bytes, "help.synthetic"); integer(run_bytes, 100, 2); integer(run_bytes, 16, 2);
    text(run_bytes, psprecomp::sha256(Bytes(512 * 512 * 4, 0))); text(run_bytes, psprecomp::sha256(glyph_signature));
    integer(run_bytes, 2, 2); integer(run_bytes, 512 * 16 * 4, 4); run_bytes.resize(run_bytes.size() + 512 * 16 * 4);
    auto runs = load(run_bytes);
    sarusaru::gpu::DrawCall glyph; glyph.through = true; glyph.primitive = sarusaru::gpu::PrimitiveType::Sprites;
    glyph.texture.enabled = true; glyph.texture.address = 0x08001000; glyph.texture.width = glyph.texture.height = glyph.texture.buffer_width = 512;
    glyph.texture.format = sarusaru::gpu::TextureFormat::Rgba8888; glyph.vertices.resize(2);
    glyph.vertices[0].position = {10, 20, 0}; glyph.vertices[1].position = {26, 36, 0};
    glyph.vertices[0].texcoord = {0, 0}; glyph.vertices[1].texcoord = {16, 16};
    auto next = glyph; next.vertices[0].position[0] = 26; next.vertices[1].position[0] = 42;
    next.vertices[0].texcoord[0] = 16; next.vertices[1].texcoord[0] = 32;
    psprecomp::GuestMemory font_memory;
    std::vector<sarusaru::gpu::DrawCall> emitted;
    auto emit = [&](const sarusaru::gpu::DrawCall &call) { emitted.push_back(call); };
    runs.translate_draw(font_memory, glyph, emit); runs.translate_draw(font_memory, next, emit); runs.flush_draws(emit);
    require(emitted.size() == 1 && emitted[0].vertices[1].position[0] == 110 &&
            runs.host_rgba(emitted[0].texture.address).size() == 512 * 16 * 4, "complete glyph run not replaced");
    emitted.clear(); runs.translate_draw(font_memory, glyph, emit); runs.flush_draws(emit);
    require(emitted.size() == 1 && emitted[0].texture.address == glyph.texture.address, "partial glyph run lost Japanese");
    emitted.clear(); auto changed_glyph = next; changed_glyph.vertices[0].texcoord[0] += 16; changed_glyph.vertices[1].texcoord[0] += 16;
    runs.translate_draw(font_memory, glyph, emit); runs.translate_draw(font_memory, changed_glyph, emit); runs.flush_draws(emit);
    require(emitted.size() == 2 && emitted[0].texture.address == glyph.texture.address, "nonmatching glyph run translated");
    emitted.clear(); font_memory.store8(glyph.texture.address + 33001, 1);
    runs.translate_draw(font_memory, glyph, emit); runs.translate_draw(font_memory, next, emit); runs.flush_draws(emit);
    require(emitted.size() == 2 && emitted[0].texture.address == glyph.texture.address, "changed font passed full-content identity");

    // Captions share the compact-message format with dialogue, beyond 88 entries.
    auto many_captions = version2(); many_captions[7] = '4';
    integer(many_captions, 0, 4); integer(many_captions, 0, 2); integer(many_captions, 100, 2);
    for (unsigned i = 0; i < 100; ++i)
        dialogue_record(many_captions, "caption.synthetic." + std::to_string(i),
                        0x3000 + i * 4, 1024 + i * 2, Bytes{0x9f, 0}, Bytes{0x9f, 0}, false);
    require(load(many_captions).dialogue_count() == 100, "expanded compact caption catalog rejected");
    auto oversized_captions = version2(); oversized_captions[7] = '4';
    integer(oversized_captions, 0, 4); integer(oversized_captions, 0, 2); integer(oversized_captions, 513, 2);
    rejects(oversized_captions);

    // A partial pack is valid: the caller can retain Japanese for all other assets.
    std::filesystem::remove(path);
    std::cout << "translation pack tests passed\n";
}
