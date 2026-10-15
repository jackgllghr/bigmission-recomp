#include "save_state/ppst.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace sarusaru::save_state;

namespace {
int failures{};
void check(bool condition, const char *message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class F> void rejects(F f, const char *message) {
    try { f(); check(false, message); } catch (const FormatError &) {}
}
void set32(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8u * i));
}

// Explicit wire bytes (not produced by our writer) exercise PPSSPP's header
// layout and Snappy's overlapping backreferences. The payload spells xababab.
std::vector<std::uint8_t> snappy_fixture(std::span<const std::uint8_t> compressed) {
    std::vector<std::uint8_t> file(176);
    set32(file, 0, 5);
    set32(file, 4, 1);
    set32(file, 8, static_cast<std::uint32_t>(compressed.size()));
    set32(file, 12, 7);
    file.insert(file.end(), compressed.begin(), compressed.end());
    return file;
}

void container_tests() {
    const PpstFile original{5, Compression::None, "v1.20.4", "SaruSaru", {1, 2, 3, 0x42, 0, 0, 0}};
    for (const auto compression : {Compression::None, Compression::Snappy, Compression::Zstandard}) {
        auto file = original;
        file.compression = compression;
        const auto encoded = encode_ppst(file);
        const auto decoded = decode_ppst(encoded);
        check(decoded.payload == file.payload && decoded.title == file.title && decoded.creator == file.creator &&
              decoded.compression == compression, "all PPSSPP compression types round trip");
        for (std::size_t length = 0; length < encoded.size(); ++length)
            rejects([&] { (void)decode_ppst(std::span(encoded).first(length)); }, "every truncation is rejected");
    }
    auto rev4 = original;
    rev4.revision = 4;
    rev4.title.clear();
    const auto encoded4 = encode_ppst(rev4);
    check(encoded4.size() == 48 + original.payload.size(), "revision 4 omits the title");
    check(decode_ppst(encoded4).payload == original.payload, "revision 4 loads");
    rev4.title = "lost title";
    rejects([&] { (void)encode_ppst(rev4); }, "revision 4 cannot silently drop a title");
    const auto wire = encode_ppst(original);
    check(wire.size() == 183 && wire[0] == 5 && wire[8] == 7 && wire[12] == 7 &&
          wire[16] == 'v' && wire[48] == 'S' && wire[176] == 1, "independent header offsets");
    for (const auto field : {0u, 4u, 8u, 12u}) {
        auto broken = wire;
        set32(broken, field, 0xFFFFFFFFu);
        rejects([&] { (void)decode_ppst(broken); }, "bad header fields are rejected before allocation");
    }
    auto trailing = wire;
    trailing.push_back(0);
    rejects([&] { (void)decode_ppst(trailing); }, "extra file bytes rejected");
    auto mismatch = wire;
    set32(mismatch, 12, 8);
    rejects([&] { (void)decode_ppst(mismatch); }, "uncompressed length mismatch rejected");
    auto zero = original;
    zero.payload.clear();
    rejects([&] { (void)encode_ppst(zero); }, "empty state rejected");
    auto metadata = original;
    metadata.creator.assign(33, 'x');
    rejects([&] { (void)encode_ppst(metadata); }, "oversized metadata rejected");
    metadata.creator.assign(32, 'x');
    check(decode_ppst(encode_ppst(metadata)).creator == metadata.creator, "full-width legacy creator is preserved");
    metadata = original;
    metadata.title = std::string("a\0b", 3);
    rejects([&] { (void)encode_ppst(metadata); }, "embedded NUL in title rejected");
    auto zstd = original;
    zstd.compression = Compression::Zstandard;
    auto damaged = encode_ppst(zstd);
    damaged[176] ^= 0x80;
    rejects([&] { (void)decode_ppst(damaged); }, "damaged Zstandard frame rejected");
    damaged = encode_ppst(zstd);
    damaged.push_back(0);
    set32(damaged, 8, static_cast<std::uint32_t>(damaged.size() - 176));
    rejects([&] { (void)decode_ppst(damaged); }, "trailing compressed data rejected");
    for (std::size_t size : {1u, 60u, 61u, 256u, 257u, 65536u, 65537u}) {
        auto large = original;
        large.compression = Compression::Snappy;
        large.payload.resize(size, 0x91u);
        check(decode_ppst(encode_ppst(large)).payload == large.payload, "Snappy literal width boundaries");
    }
}

void snappy_tests() {
    const std::vector<std::uint8_t> expected{'x', 'a', 'b', 'a', 'b', 'a', 'b'};
    for (const std::vector<std::uint8_t> bytes : {
            std::vector<std::uint8_t>{7, 8, 'x', 'a', 'b', 1, 2},
            std::vector<std::uint8_t>{7, 8, 'x', 'a', 'b', 14, 2, 0},
            std::vector<std::uint8_t>{7, 8, 'x', 'a', 'b', 15, 2, 0, 0, 0}})
        check(decode_ppst(snappy_fixture(bytes)).payload == expected, "all Snappy copy tags overlap correctly");
    for (const std::vector<std::uint8_t> bytes : {
            std::vector<std::uint8_t>{7, 1, 0}, // zero copy distance
            std::vector<std::uint8_t>{7, 1, 2}, // copy before output
            std::vector<std::uint8_t>{7, 8, 'x', 'a', 'b', 1, 0},
            std::vector<std::uint8_t>{7, 8, 'x', 'a', 'b', 1, 4},
            std::vector<std::uint8_t>{7, 8, 'x', 'a', 'b', 5, 2}, // expands past header length
            std::vector<std::uint8_t>{7, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF},
            std::vector<std::uint8_t>{0x80, 0x80, 0x80, 0x80, 0x10},
            std::vector<std::uint8_t>{8, 8, 'x', 'a', 'b', 1, 2}})
        rejects([&] { (void)decode_ppst(snappy_fixture(bytes)); }, "invalid Snappy stream rejected");
}

void payload_tests() {
    PayloadWriter w;
    w.section("Outer", 3);
    w.u32(0x98765432u);
    w.section("1234567890123456", 1);
    w.u64(0x0123456789ABCDEFull);
    w.boolean(true);
    w.string("saved thread");
    w.end_section();
    w.end_section();
    PayloadReader r(w.data());
    check(r.section("Outer", 1, 3) == 3 && r.u32() == 0x98765432u, "versioned section and LE32 read");
    check(r.section("1234567890123456", 1, 1) == 1 && r.u64() == 0x0123456789ABCDEFull,
          "full-width nested section and LE64 read");
    check(r.boolean() && r.string() == "saved thread", "bool and string wire representation");
    r.end_section();
    r.end_section();
    r.finish();
    rejects([&] { (void)r.u8(); }, "reads beyond end rejected");
    PayloadReader wrong_version(w.data());
    rejects([&] { (void)wrong_version.section("Outer", 1, 2); }, "future section version rejected");
    PayloadReader wrong_name(w.data());
    rejects([&] { (void)wrong_name.section("Other", 1, 3); }, "section names checked");
    const std::array<std::uint8_t, 1> bad_bool{2};
    PayloadReader b(bad_bool);
    rejects([&] { (void)b.boolean(); }, "invalid bool rejected");
    const std::array<std::uint8_t, 5> bad_string{1, 0, 0, 0, 'x'};
    PayloadReader s(bad_string);
    rejects([&] { (void)s.string(); }, "string needs in-bounds terminator");
    const std::array<std::uint8_t, 4> bad_cookie{0x43, 0, 0, 0};
    PayloadReader c(bad_cookie);
    rejects([&] { c.end_section(); }, "closing cookie checked");
    PayloadReader unfinished(w.data());
    rejects([&] { unfinished.finish(); }, "unfinished payload rejected");
}

void file_tests() {
    const auto directory = std::filesystem::temp_directory_path() / ("sarusaru-ppst-test-" +
        std::to_string(static_cast<std::uint64_t>(std::filesystem::file_time_type::clock::now().time_since_epoch().count())));
    std::filesystem::create_directory(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{directory};
    const auto path = directory / "UCJS10060_1.00_0.ppst";
    PpstFile state{5, Compression::Zstandard, "v1.20.4", "title", {1, 2, 3}};
    write_ppst(path, state);
    state.payload = {9, 8, 7};
    write_ppst(path, state);
    check(read_ppst(path).payload == state.payload, "existing state replaced atomically");
    auto invalid = state;
    invalid.creator.assign(33, 'x');
    rejects([&] { write_ppst(path, invalid); }, "invalid save fails before replacing destination");
    check(read_ppst(path).payload == state.payload, "failed write preserves previous state");
    std::filesystem::create_directory(directory / "destination-directory");
    rejects([&] { write_ppst(directory / "destination-directory", state); }, "rename failure reported");
    rejects([&] { write_ppst(directory / "missing" / "state.ppst", state); }, "missing parent reported");
    rejects([&] { (void)read_ppst(directory / "missing.ppst"); }, "missing state reported");
    check(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}) == 2,
          "failed writes clean their temporary files");
}
} // namespace

int main() {
    container_tests();
    snappy_tests();
    payload_tests();
    file_tests();
    std::cout << (failures ? "FAIL" : "PASS") << ": PPSSPP state container (" << failures << " failures)\n";
    return failures != 0;
}
