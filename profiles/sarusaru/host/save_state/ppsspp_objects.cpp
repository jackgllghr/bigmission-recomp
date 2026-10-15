#include "save_state/ppsspp_objects.hpp"

#include <algorithm>
#include <array>

namespace sarusaru::save_state {
namespace {
constexpr std::uint32_t pool_size = 4096, first_uid = 0x100;
std::uint32_t count(PayloadReader &r, std::size_t width, std::uint32_t maximum = 65536) {
    const auto n = r.u32();
    if (n > maximum || n > r.remaining() / width) throw FormatError("PPST: invalid kernel collection size");
    return n;
}
void sequence(PayloadReader &r, std::size_t width, std::uint32_t maximum = 65536) {
    const auto n = count(r, width, maximum);
    (void)r.bytes(static_cast<std::size_t>(n) * width);
}
std::string name(std::span<const std::uint8_t> field) {
    return {field.begin(), std::find(field.begin(), field.end(), 0)};
}
void section(PayloadReader &r, std::string_view title, std::uint32_t version) {
    (void)r.section(title, version, version);
}

void hle(PayloadReader &r) {
    section(r, "HLE", 2);
    (void)r.u32(); // Delayed-result event type.
    const auto slots = count(r, 4, 4096);
    for (std::uint32_t i = 0; i < slots; ++i) {
        if (r.u32() != 0xFFFFFFFFu) throw FormatError("PPST: active HLE action schema is not supported yet");
    }
    r.end_section();
}

void file_info(PayloadReader &r) {
    section(r, "PSPFileInfo", 1);
    (void)r.string(); (void)r.u64(); (void)r.u32(); (void)r.boolean(); (void)r.u32();
    for (unsigned i = 0; i < 3; ++i) {
        if (r.u32() != 0x13376D74u) throw FormatError("PPST: legacy host-specific timestamp is unsupported");
        (void)r.bytes(9u * 4u);
    }
    (void)r.boolean(); (void)r.bytes(3u * 4u);
    r.end_section();
}

std::string object(PayloadReader &r, PspObjectType type) {
    std::string result;
    switch (type) {
    case PspObjectType::Thread: {
        section(r, "Thread", 5);
        const auto native = r.bytes(104);
        result = name(native.subspan(4, 32));
        (void)r.bytes(8u + 4u); // wait value/timeout pointer and module UID
        (void)r.boolean();
        (void)r.bytes(8u); // current MIPS-call and callback IDs
        (void)r.bytes(214u * 4u); // PSP thread context, including sixth union word
        sequence(r, 4, 4096); // callbacks
        sequence(r, 4, 4096); // pending MIPS calls
        sequence(r, 8, 4096); // extended stacks
        (void)r.bytes(8); // active stack
        sequence(r, 4, 4096); // thread-end waiters
        sequence(r, 12, 4096); // callback/timeout map, serialized key then value
        break;
    }
    case PspObjectType::Callback: {
        section(r, "Callback", 1);
        const auto native = r.bytes(56);
        result = name(native.subspan(4, 32));
        (void)r.bytes(20); // legacy saved registers
        break;
    }
    case PspObjectType::Semaphore: {
        section(r, "Semaphore", 1);
        const auto native = r.bytes(56);
        result = name(native.subspan(4, 32));
        sequence(r, 4, 4096);
        sequence(r, 12, 4096);
        break;
    }
    case PspObjectType::EventFlag: {
        section(r, "EventFlag", 1);
        const auto native = r.bytes(52);
        result = name(native.subspan(4, 32));
        sequence(r, 24, 4096); // waiter POD includes its 64-bit paused timeout
        sequence(r, 28, 4096); // explicit key + the waiter POD
        break;
    }
    case PspObjectType::PartitionMemory: {
        section(r, "PMB", 2);
        (void)r.u32(); result = name(r.bytes(32)); (void)r.u32();
        break;
    }
    case PspObjectType::Module: {
        section(r, "Module", 6);
        const auto native = r.bytes(196);
        result = name(native.subspan(8, 28));
        (void)r.bytes(12); // CRC, allocation address, allocation size
        (void)r.boolean(); // fake/HLE module
        (void)r.bytes(20); // text bounds, import-stub bounds, guest module pointer
        sequence(r, 8, 4096); // waiting threads
        sequence(r, 40); // exported functions
        sequence(r, 40); // imported functions
        sequence(r, 40); // exported variables
        sequence(r, 44); // imported variables (wire POD includes tail padding)
        section(r, "Plugins", 1);
        (void)r.boolean();
        r.end_section();
        break;
    }
    case PspObjectType::File: {
        section(r, "FileNode", 3);
        result = r.string();
        (void)r.bytes(12); (void)r.u64();
        for (unsigned i = 0; i < 4; ++i) (void)r.boolean();
        file_info(r);
        (void)r.bytes(12); // open mode, NPDRM flag, PGD offset
        if (r.boolean()) throw FormatError("PPST: encrypted PGD file schema is unsupported");
        sequence(r, 4, 4096); sequence(r, 4, 4096);
        (void)r.boolean(); sequence(r, 12, 4096);
        break;
    }
    case PspObjectType::Directory: {
        section(r, "DirListing", 1);
        result = r.string(); (void)r.u32();
        const auto entries = count(r, 24);
        for (std::uint32_t i = 0; i < entries; ++i) file_info(r);
        break;
    }
    default:
        throw FormatError("PPST: unsupported kernel object type " + std::to_string(static_cast<std::uint32_t>(type)));
    }
    r.end_section();
    return result;
}
void remainder(std::span<const std::uint8_t> bytes) {
    PayloadReader r(bytes);
    section(r, "Kernel Modules", 1);
    if (bytes.size() < 24) throw FormatError("PPST: missing kernel module records");
    PayloadReader end(bytes.last(4));
    end.end_section();
}
} // namespace

PspKernelObjects decode_psp_objects(std::span<const std::uint8_t> tail) {
    if (tail.size() > kMaxPpstSize) throw FormatError("PPST: kernel tail exceeds size limit");
    PayloadReader r(tail);
    PspKernelObjects result;
    hle(r);
    result.hle_record.assign(tail.begin(), tail.begin() + static_cast<std::ptrdiff_t>(r.offset()));
    section(r, "Kernel", 2);
    result.running = r.boolean();
    section(r, "KernelObjectPool", 1);
    if (r.u32() != pool_size) throw FormatError("PPST: unsupported kernel object pool size");
    result.next_pool_index = r.u32();
    if (result.next_pool_index >= pool_size) throw FormatError("PPST: invalid next kernel object index");
    std::array<bool, pool_size> used{};
    for (auto &slot : used) slot = r.boolean();
    for (std::uint32_t i = 0; i < pool_size; ++i) {
        if (!used[i]) continue;
        PspObjectRecord record;
        record.type = static_cast<PspObjectType>(r.u32());
        const auto begin = r.offset();
        record.name = object(r, record.type);
        const auto encoded = tail.subspan(begin, r.offset() - begin);
        record.encoded.assign(encoded.begin(), encoded.end());
        result.objects.emplace(first_uid + i, std::move(record));
    }
    r.end_section();
    result.exit_callback = r.u32();
    r.end_section();
    const auto remaining = r.bytes(r.remaining());
    remainder(remaining);
    result.remainder.assign(remaining.begin(), remaining.end());
    return result;
}

std::vector<std::uint8_t> encode_psp_objects(const PspKernelObjects &objects) {
    if (objects.next_pool_index >= pool_size || objects.objects.size() > pool_size)
        throw FormatError("PPST: invalid kernel object pool");
    PayloadReader hle_reader(objects.hle_record);
    hle(hle_reader); hle_reader.finish();
    remainder(objects.remainder);
    for (const auto &[uid, record] : objects.objects) {
        if (uid < first_uid || uid >= first_uid + pool_size) throw FormatError("PPST: object UID outside kernel pool");
        PayloadReader r(record.encoded);
        (void)object(r, record.type); r.finish();
    }
    PayloadWriter w;
    w.bytes(objects.hle_record);
    w.section("Kernel", 2); w.boolean(objects.running);
    w.section("KernelObjectPool", 1); w.u32(pool_size); w.u32(objects.next_pool_index);
    for (std::uint32_t i = 0; i < pool_size; ++i) w.boolean(objects.objects.contains(first_uid + i));
    for (const auto &[uid, record] : objects.objects) {
        (void)uid;
        w.u32(static_cast<std::uint32_t>(record.type)); w.bytes(record.encoded);
    }
    w.end_section(); w.u32(objects.exit_callback); w.end_section();
    w.bytes(objects.remainder);
    return w.data();
}
} // namespace sarusaru::save_state
