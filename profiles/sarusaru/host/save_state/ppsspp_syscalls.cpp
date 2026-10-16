#include "save_state/ppsspp_syscalls.hpp"

#include <algorithm>

namespace sarusaru::save_state {
namespace {
constexpr std::uint32_t jr_ra = 0x03E00008u;
std::uint32_t count(PayloadReader &r, std::size_t width) {
    const auto n = r.u32();
    if (n > 65536 || n > r.remaining() / width) throw FormatError("PPST: invalid module symbol collection");
    return n;
}
void skip(PayloadReader &r, std::size_t width) { (void)r.bytes(static_cast<std::size_t>(count(r, width)) * width); }
std::uint32_t word(const MachinePrefix &machine, std::uint32_t address) {
    const auto physical = address & 0x3FFFFFFFu;
    constexpr auto base = psprecomp::GuestMemory::kPhysicalBase;
    if ((address & 3u) != 0 || physical < base || std::uint64_t(physical - base) + 4 > machine.ram.size())
        throw FormatError("PPST: import stub outside aligned guest RAM");
    PayloadReader r(std::span(machine.ram).subspan(physical - base, 4)); return r.u32();
}
} // namespace
PspSyscallTable PspSyscallTable::decode(const MachinePrefix &machine, const PspKernelObjects &objects) {
    PspSyscallTable result;
    if (objects.objects.size() > 4096) throw FormatError("PPST: too many modules");
    for (const auto &[uid, record] : objects.objects) {
        (void)uid;
        if (record.type != PspObjectType::Module) continue;
        PayloadReader r(record.encoded); (void)r.section("Module", 6, 6);
        (void)r.bytes(196 + 12); (void)r.boolean(); (void)r.bytes(20);
        skip(r, 8); skip(r, 40); // module waiters and function exports
        for (auto n = count(r, 40); n != 0; --n) {
            const auto name = r.bytes(32);
            const auto end = std::find(name.begin(), name.end(), 0);
            if (end == name.end() || end == name.begin()) throw FormatError("PPST: invalid import library name");
            PspImportBinding binding{std::string(name.begin(), end), 0};
            const auto address = r.u32(); binding.nid = r.u32();
            const auto first = word(machine, address);
            // Resolved guest exports jump to guest code, which the interpreter
            // executes normally. Only JR RA + SYSCALL denotes an HLE import.
            if (first != jr_ra) continue;
            if (address > 0xFFFFFFFBu) throw FormatError("PPST: overflowing import stub");
            const auto instruction = word(machine, address + 4);
            if ((instruction & 0xFC00003Fu) != 0x0000000Cu) continue;
            const auto code = (instruction >> 6) & 0xFFFFFu;
            // PPSSPP's unresolved-import sentinel has no unique symbol. Keep
            // it unbound so it cannot call an arbitrary missing import.
            if (code == 0xFFFFFu) continue;
            const auto [found, inserted] = result.imports_.emplace(code, binding);
            if (!inserted && found->second != binding) throw FormatError("PPST: conflicting syscall symbol bindings");
        }
        skip(r, 40); skip(r, 44); // variable exports/imports
        (void)r.section("Plugins", 1, 1); (void)r.boolean(); r.end_section(); r.end_section(); r.finish();
    }
    return result;
}
void PspSyscallTable::install(psprecomp::Runtime &runtime) const {
    runtime.set_syscall_handler([imports = imports_](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu, std::uint32_t code) {
        const auto binding = imports.find(code);
        if (binding == imports.end()) {
            rt.stop("Unmapped PPSSPP syscall " + std::to_string(code));
            return;
        }
        rt.invoke_import(binding->second.library, binding->second.nid, cpu);
    });
}
} // namespace sarusaru::save_state
