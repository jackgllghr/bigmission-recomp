#include "save_state/slots.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>

namespace sarusaru::save_state {
namespace {
constexpr std::array<std::uint8_t,8> kMagic{'B','M','S','L','O','T','0','1'};
constexpr std::size_t kHeaderSize=40;
constexpr std::size_t kMaxPreviewBytes=kThumbnailWidth*kThumbnailHeight*4;
struct Header { std::uint64_t timestamp{}, state_size{}; std::uint32_t width{},height{},preview_size{}; };
bool magic(std::span<const std::uint8_t> bytes) {
    return bytes.size()>=kMagic.size() && std::equal(kMagic.begin(),kMagic.end(),bytes.begin());
}
Header header(std::span<const std::uint8_t> bytes, std::uint64_t file_size) {
    PayloadReader r(bytes);
    if (!magic(r.bytes(8)) || r.u32()!=1) throw FormatError("Unsupported state slot header");
    Header h; h.timestamp=r.u64(); h.width=r.u32(); h.height=r.u32(); h.preview_size=r.u32(); h.state_size=r.u64();
    if (h.timestamp>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        h.width!=kThumbnailWidth || h.height!=kThumbnailHeight || h.preview_size!=kMaxPreviewBytes ||
        h.state_size==0 || h.state_size>kMaxPpstSize || file_size!=kHeaderSize+h.preview_size+h.state_size)
        throw FormatError("Invalid state slot sizes");
    return h;
}
std::array<std::uint8_t,kHeaderSize> read_header(std::ifstream &input) {
    std::array<std::uint8_t,kHeaderSize> bytes{};
    input.read(reinterpret_cast<char *>(bytes.data()),bytes.size());
    if (!input) throw FormatError("Truncated state slot header");
    return bytes;
}
std::uint64_t size(std::ifstream &input) {
    input.seekg(0,std::ios::end); const auto end=input.tellg(); input.seekg(0);
    if (end<0 || static_cast<std::uint64_t>(end)>kMaxPpstSize+kHeaderSize+kMaxPreviewBytes)
        throw FormatError("Invalid state slot file size");
    return static_cast<std::uint64_t>(end);
}
}
std::filesystem::path slot_path(const std::filesystem::path &settings,std::uint32_t slot) {
    if (slot<1 || slot>kSlotCount) throw FormatError("Save-state slot must be between 1 and 5");
    return settings.parent_path()/"states"/(slot==1 ? "UCJS10060.quick.bmstate" : "UCJS10060.slot"+std::to_string(slot)+".bmstate");
}
SlotInfo slot_info(const std::filesystem::path &path) {
    SlotInfo info;
    std::error_code error; info.occupied=std::filesystem::is_regular_file(path,error);
    if (!info.occupied) return info;
    try {
        const auto modified=std::filesystem::last_write_time(path);
        const auto time=modified-decltype(modified)::clock::now()+std::chrono::system_clock::now();
        info.saved_at=std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
        std::ifstream input(path,std::ios::binary); const auto file_size=size(input);
        const auto bytes=read_header(input);
        if (!magic(bytes)) return info;
        const auto h=header(bytes,file_size);
        info.saved_at=static_cast<std::int64_t>(h.timestamp);
        Thumbnail thumbnail{h.width,h.height,std::vector<std::uint8_t>(h.preview_size)};
        input.read(reinterpret_cast<char *>(thumbnail.rgba.data()),h.preview_size);
        if (input) info.thumbnail=std::move(thumbnail);
    } catch (const std::exception &) { /* A broken preview does not hide an occupied slot. */ }
    return info;
}
void write_slot(const std::filesystem::path &path,const PpstFile &state,const Thumbnail &thumbnail) {
    if (thumbnail.rgba.empty()) { write_ppst(path,state); return; }
    if (thumbnail.width!=kThumbnailWidth || thumbnail.height!=kThumbnailHeight || thumbnail.rgba.size()!=kMaxPreviewBytes)
        throw FormatError("Invalid state thumbnail dimensions");
    const auto container=encode_ppst(state);
    PayloadWriter out; out.bytes(kMagic); out.u32(1);
    out.u64(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
    out.u32(thumbnail.width); out.u32(thumbnail.height); out.u32(static_cast<std::uint32_t>(thumbnail.rgba.size()));
    out.u64(container.size()); out.bytes(thumbnail.rgba); out.bytes(container);
    write_atomic_file(path,out.data());
}
PpstFile read_slot(const std::filesystem::path &path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) throw FormatError("Cannot open saved state");
    const auto file_size=size(input); const auto bytes=read_header(input);
    if (!magic(bytes)) { input.close(); return read_ppst(path); }
    const auto h=header(bytes,file_size);
    input.seekg(static_cast<std::streamoff>(kHeaderSize+h.preview_size));
    std::vector<std::uint8_t> container(static_cast<std::size_t>(h.state_size));
    input.read(reinterpret_cast<char *>(container.data()),static_cast<std::streamsize>(container.size()));
    if (!input || input.peek()!=std::char_traits<char>::eof()) throw FormatError("State slot changed while reading");
    return decode_ppst(container);
}
}
