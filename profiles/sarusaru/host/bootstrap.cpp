#include "session.hpp"
#include "psprecomp/common.hpp"

#include <cstdlib>
#include <charconv>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: sarusaru_bootstrap <prepared BOOT.elf> <game ISO>\n"
                     "Headless diagnostic run: no window, no sound. Exit 3 reports why it stopped.\n";
        return 0;
    }
    if (argc != 3) {
        std::cerr << "Usage: sarusaru_bootstrap <prepared BOOT.elf> <game ISO>\n";
        return 2;
    }
    try {
        sarusaru::SessionOptions options;
        // Counting every dispatch keeps the budget meaningful; SARUSARU_CHAINING=1
        // runs the faster chained mode the windowed shell uses.
        options.count_every_dispatch = std::getenv("SARUSARU_CHAINING") == nullptr;
        options.memory_stick = sarusaru::utility::default_memory_stick();
        if (const char *value = std::getenv("SARUSARU_EXPERIMENTAL_GAME_FRAME_RATE"); value && *value) {
            const std::string_view text(value);
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), options.experimental_game_frame_rate);
            if (error != std::errc{} || end != text.data() + text.size())
                throw psprecomp::Error("Invalid SARUSARU_EXPERIMENTAL_GAME_FRAME_RATE");
            if (options.experimental_game_frame_rate != 0)
                std::cerr << "Experimental game frame rate " << options.experimental_game_frame_rate
                          << "; fixed-step gameplay/animation speed is not compensated\n";
        }
        sarusaru::GameSession game(argv[1], argv[2], options);
        // SARUSARU_INPUT scripts the pad for unattended runs, e.g. "60:START;60.2:-".
        if (const char *input = std::getenv("SARUSARU_INPUT"); input != nullptr && *input != '\0') {
            auto script = sarusaru::ctrl::parse_input_script(input);
            if (!script) throw psprecomp::Error("Malformed SARUSARU_INPUT script");
            game.controller.set_script(std::move(*script));
        }
        std::cout << "Memory stick: " << game.savedata.memory_stick().string() << '\n'
                  << "SaruSaru diagnostic AOT bootstrap (headless)\n"
                  << "Module: " << game.module_name() << "; entry: " << psprecomp::hex32(game.entry())
                  << "; registered entries: " << game.runtime.function_count() << '\n' << std::flush;
        // SARUSARU_DISPATCH_LIMIT raises the diagnostic budget for longer runs.
        std::uint64_t dispatch_limit = 100'000u;
        if (const char *limit = std::getenv("SARUSARU_DISPATCH_LIMIT"); limit != nullptr && *limit != '\0')
            dispatch_limit = std::strtoull(limit, nullptr, 10);
        game.run(dispatch_limit);
        game.runtime.report_hle_histogram(25u);  // PSPRECOMP_HLE_HISTOGRAM=1 enables it
        const auto &cpu = game.runtime.cpu();
        auto &ge = game.ge;
        std::cout << "Stopped at " << psprecomp::hex32(cpu.pc) << ": " << game.runtime.stop_reason() << '\n'
                  << "Guest time: " << game.kernel.now_us() << " us (virtual); GE: " << ge.stats().lists_completed
                  << " lists, " << ge.stats().commands << " commands, " << ge.stats().primitives << " primitives; "
                  << ge.state().draw_count() << " draws decoded ("
                  << ge.state().vertex_count() << " vertices, "
                  << ge.state().unhandled_command_count() << " unhandled commands)\n"
                  << "Game frames overridden: " << game.frame_rate.overridden_frames()
                  << "; interval: " << game.frame_rate.interval_vblanks() << " vblanks (0 = native)\n"
                  << "Stop arguments: a0=" << psprecomp::hex32(cpu.gpr[4]) << " a1=" << psprecomp::hex32(cpu.gpr[5])
                  << " a2=" << psprecomp::hex32(cpu.gpr[6]) << " a3=" << psprecomp::hex32(cpu.gpr[7]) << '\n'
                  << "Observed startup values: compiled SDK "
                  << psprecomp::hex32(game.startup_hle.compiled_sdk_version()) << ", compiler "
                  << psprecomp::hex32(game.startup_hle.compiler_version()) << '\n'
                  << "Only explicitly modeled imports are bound; every other missing call stops execution.\n";
        return 3;
    } catch (const std::exception &error) {
        std::cerr << "sarusaru_bootstrap: " << error.what() << '\n';
        return 1;
    }
}
