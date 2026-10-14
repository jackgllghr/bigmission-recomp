#include "translation/texture_pack.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace sarusaru::translation {
namespace {
[[noreturn]] void invalid(std::string_view reason) {
    throw std::runtime_error("translation dialogue: " + std::string(reason));
}

std::uint32_t mapped(const psprecomp::Elf32Image &elf, const psprecomp::GuestMemory &memory,
                     std::uint32_t offset, std::uint32_t size, std::uint32_t base,
                     std::string_view section_name, std::uint32_t flags) {
    const bool section_ok = std::ranges::any_of(elf.sections(), [&](const auto &section) {
        return section.name == section_name && section.type == 1 && section.flags == flags &&
               offset >= section.offset && static_cast<std::uint64_t>(offset) + size <=
               static_cast<std::uint64_t>(section.offset) + section.size;
    });
    if (!section_ok) invalid("source/lookup outside its declared section");
    std::optional<std::uint32_t> result;
    for (std::size_t i = 0; i < elf.segments().size(); ++i) {
        const auto &segment = elf.segments()[i];
        if (segment.type != 1 || offset < segment.offset || static_cast<std::uint64_t>(offset) + size >
            static_cast<std::uint64_t>(segment.offset) + segment.file_size) continue;
        const auto address = static_cast<std::uint64_t>(elf.segment_runtime_address(i, base)) + offset - segment.offset;
        if (result || address > UINT32_MAX || !memory.contains(static_cast<std::uint32_t>(address), size))
            invalid("unmapped or ambiguous source extent");
        result = static_cast<std::uint32_t>(address);
    }
    if (!result) invalid("unmapped source extent");
    return *result;
}

// Returns protected button/control identities, checking the native compact
// grammar (including zero trail bytes) and optional English layout budgets.
std::vector<std::uint32_t> controls(std::span<const std::uint8_t> bytes, bool layout) {
    std::vector<std::uint32_t> result;
    std::size_t at = 0;
    unsigned columns = 0, lines = 1;
    while (at < bytes.size()) {
        const auto byte = bytes[at++];
        if (byte == 0) {
            if (at != bytes.size()) invalid("data after message terminator");
            std::ranges::sort(result);
            return result;
        }
        if (byte == 0xff) { result.push_back(0x10001); columns = 0; lines = 1; continue; }
        if (byte == 0xfe) {
            columns = 0;
            if (++lines > 3 && layout) invalid("English dialogue exceeds line budget");
            continue;
        }
        if (byte == 0xfd) { result.push_back(0x10000); continue; }
        std::uint32_t index = byte - 1u;
        if (byte >= 0xf0) {
            if (at == bytes.size()) invalid("truncated compact glyph");
            index = 239u + ((byte - 0xf0u) << 8u) + bytes[at++];
        }
        if (index >= 1024) invalid("glyph outside supported atlas");
        if (++columns > 20 && layout) invalid("English dialogue exceeds column budget");
        // CP932's native triangle/circle/square/cross cells in the basic range.
        if (index == 98 || index == 91 || index == 96 || index == 62) result.push_back(index);
    }
    invalid("missing compact message terminator");
}
}

void TexturePack::apply_dialogue(psprecomp::GuestMemory &memory, const psprecomp::Elf32Image &elf,
                               std::uint32_t load_base, const DialogueAllocator &allocate) const {
    if (dialogue_.empty()) return;
    std::vector<std::uint32_t> pointers, offsets;
    std::uint32_t total = 0;
    for (const auto &entry : dialogue_) {
        const auto source = mapped(elf, memory, entry.source_offset, entry.source_size, load_base, ".rodata", 2);
        const auto pointer = mapped(elf, memory, entry.pointer_offset, 4, load_base, ".data", 3);
        if (memory.load32(pointer) != source) invalid("message lookup pointer changed: " + entry.id);
        std::vector<std::uint8_t> bytes(entry.source_size);
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = memory.load8(source + i);
        if (psprecomp::sha256(bytes) != entry.source_hash) invalid("message source changed: " + entry.id);
        if (controls(bytes, false) != controls(entry.encoded, true)) invalid("protected controls changed: " + entry.id);
        offsets.push_back(total);
        total += static_cast<std::uint32_t>(entry.encoded.size());
        pointers.push_back(pointer);
    }
    // No lookup changes until all sources and the reserved destination validate.
    const auto destination = allocate(total);
    if (destination == 0 || !memory.contains(destination, total)) invalid("cannot reserve dialogue memory");
    for (std::size_t i = 0; i < elf.segments().size(); ++i) {
        const auto &segment = elf.segments()[i];
        if (segment.type != 1) continue;
        const auto start = elf.segment_runtime_address(i, load_base);
        if (destination < static_cast<std::uint64_t>(start) + segment.memory_size &&
            start < static_cast<std::uint64_t>(destination) + total) invalid("dialogue memory overlaps executable");
    }
    for (std::size_t i = 0; i < dialogue_.size(); ++i) {
        const auto &bytes = dialogue_[i].encoded;
        for (std::size_t j = 0; j < bytes.size(); ++j) memory.store8(destination + offsets[i] + j, bytes[j]);
    }
    for (std::size_t i = 0; i < pointers.size(); ++i) memory.store32(pointers[i], destination + offsets[i]);
}
} // namespace sarusaru::translation
