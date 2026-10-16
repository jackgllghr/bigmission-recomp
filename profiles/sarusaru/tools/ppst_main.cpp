#include "save_state/machine.hpp"
#include "save_state/ppsspp_objects.hpp"
#include "save_state/ppsspp_scheduler.hpp"
#include "save_state/ppsspp_thread.hpp"
#include "save_state/ppsspp_syscalls.hpp"

#include <iomanip>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: sarusaru_ppst inspect FILE.ppst\n"
                     "       sarusaru_ppst objects FILE.ppst\n"
                     "       sarusaru_ppst scheduler FILE.ppst\n"
                     "       sarusaru_ppst verify FILE.ppst\n"
                     "       sarusaru_ppst repack INPUT.ppst OUTPUT.ppst [none|snappy|zstd]\n"
                     "Inspects PPSSPP 1.20.4 machine records or rewrites their container.\n"
                     "Repacking preserves kernel/HLE records verbatim; it does not resume a game.\n";
        return 0;
    }
    try {
        if (argc < 3) throw sarusaru::save_state::FormatError("use --help for usage");
        auto file = sarusaru::save_state::read_ppst(argv[2]);
        auto machine = sarusaru::save_state::decode_machine_prefix(file.payload);
        if (std::string_view(argv[1]) == "inspect" && argc == 3) {
            std::cout << "PPST revision: " << file.revision << "\nCreator: " << file.creator
                      << "\nTitle: " << file.title << "\nPayload bytes: " << file.payload.size()
                      << "\nRAM bytes: " << machine.ram.size() << "\nPC: 0x" << std::hex << machine.cpu.pc
                      << std::dec << "\nIn delay slot: " << machine.cpu.in_delay_slot
                      << "\nPending timing events: " << machine.events.size()
                      << "\nKernel/HLE/filesystem bytes retained: " << machine.remainder.size() << '\n';
        } else if (std::string_view(argv[1]) == "objects" && argc == 3) {
            const auto objects = sarusaru::save_state::decode_psp_objects(machine.remainder);
            for (const auto &[uid, record] : objects.objects) {
                if (record.type == sarusaru::save_state::PspObjectType::Thread &&
                    sarusaru::save_state::encode_psp_thread(sarusaru::save_state::decode_psp_thread(record.encoded)) != record.encoded)
                    throw sarusaru::save_state::FormatError("typed thread record did not round-trip byte for byte");
                if (record.type == sarusaru::save_state::PspObjectType::Semaphore &&
                    sarusaru::save_state::encode_psp_semaphore(sarusaru::save_state::decode_psp_semaphore(record.encoded)) != record.encoded)
                    throw sarusaru::save_state::FormatError("typed semaphore record did not round-trip byte for byte");
                if (record.type == sarusaru::save_state::PspObjectType::EventFlag &&
                    sarusaru::save_state::encode_psp_event_flag(sarusaru::save_state::decode_psp_event_flag(record.encoded)) != record.encoded)
                    throw sarusaru::save_state::FormatError("typed event flag record did not round-trip byte for byte");
                if (record.type == sarusaru::save_state::PspObjectType::Callback &&
                    sarusaru::save_state::encode_psp_callback(sarusaru::save_state::decode_psp_callback(record.encoded)) != record.encoded)
                    throw sarusaru::save_state::FormatError("typed callback record did not round-trip byte for byte");
                std::cout << "UID 0x" << std::hex << uid << " type 0x" << static_cast<std::uint32_t>(record.type)
                          << std::dec << " " << record.name << " (" << record.encoded.size() << " bytes)\n";
            }
            if (sarusaru::save_state::encode_psp_objects(objects) != machine.remainder)
                throw sarusaru::save_state::FormatError("kernel object records did not round-trip byte for byte");
        } else if (std::string_view(argv[1]) == "scheduler" && argc == 3) {
            const auto objects = sarusaru::save_state::decode_psp_objects(machine.remainder);
            const auto scheduler = sarusaru::save_state::decode_psp_scheduler(objects.remainder);
            const auto syscalls = sarusaru::save_state::PspSyscallTable::decode(machine, objects);
            if (sarusaru::save_state::encode_psp_scheduler(scheduler) != objects.remainder)
                throw sarusaru::save_state::FormatError("scheduler records did not round-trip byte for byte");
            std::cout << "Current thread: 0x" << std::hex << scheduler.current_thread << std::dec
                      << "\nThreads: " << scheduler.threads.size() << "\nPending interrupts: " << scheduler.pending_interrupts.size();
            for (std::size_t i = 0; i < scheduler.allocators.size(); ++i)
                std::cout << "\nAllocator " << i << " blocks: " << scheduler.allocators[i].blocks.size();
            std::cout << "\nScheduler records round-trip byte for byte.\n";
            std::cout << "Resolved HLE syscall bindings: " << syscalls.imports().size() << '\n';
        } else if (std::string_view(argv[1]) == "verify" && argc == 3) {
            if (sarusaru::save_state::encode_machine_prefix(machine) != file.payload)
                throw sarusaru::save_state::FormatError("machine prefix did not round-trip byte for byte");
            std::cout << "Machine prefix round-trips byte for byte; retained subsystem records are opaque.\n";
        } else if (std::string_view(argv[1]) == "repack" && (argc == 4 || argc == 5)) {
            if (argc == 5) {
                const std::string_view compression(argv[4]);
                if (compression == "none") file.compression = sarusaru::save_state::Compression::None;
                else if (compression == "snappy") file.compression = sarusaru::save_state::Compression::Snappy;
                else if (compression == "zstd") file.compression = sarusaru::save_state::Compression::Zstandard;
                else throw sarusaru::save_state::FormatError("unknown compression");
            }
            file.payload = sarusaru::save_state::encode_machine_prefix(machine);
            sarusaru::save_state::write_ppst(argv[3], file);
        } else throw sarusaru::save_state::FormatError("invalid arguments; use --help");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "sarusaru_ppst: " << error.what() << '\n';
        return 1;
    }
}
