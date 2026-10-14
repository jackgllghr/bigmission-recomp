#include "session.hpp"

#include "game_identity.hpp"
#include "psprecomp/common.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace sarusaru {
namespace {

psprecomp::Elf32Image load_executable(const std::filesystem::path &path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != executable_size)
        throw psprecomp::Error("Unsupported executable size; prepare your compatible ISO first");
    auto elf = psprecomp::Elf32Image::from_file(path);
    if (psprecomp::sha256(elf.bytes()) != executable_sha256)
        throw psprecomp::Error("Unsupported executable hash; prepare your compatible ISO first");
    return elf;
}

// The disc must be the one the executable was prepared from: its UMD_DATA.BIN
// names the supported disc and its BOOT.BIN is byte-identical to BOOT.elf.
void verify_disc(umd::DiscImage &disc) {
    const auto umd_data = disc.find("UMD_DATA.BIN");
    if (!umd_data) throw psprecomp::Error("Disc image has no UMD_DATA.BIN");
    const auto bytes = disc.read_file(*umd_data, 4096u);
    const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    const std::string expected = std::string(disc_id.substr(0u, 4u)) + "-" + std::string(disc_id.substr(4u)) + "|";
    if (!text.starts_with(expected)) throw psprecomp::Error("Disc image is not the supported game disc");
    const auto boot = disc.find("PSP_GAME/SYSDIR/BOOT.BIN");
    if (!boot || boot->size != executable_size || psprecomp::sha256(disc.read_file(*boot, boot->size)) != executable_sha256)
        throw psprecomp::Error("Disc image executable does not match the prepared executable");
}

bool configure_chaining(bool count_every_dispatch) {
    if (count_every_dispatch) {
#if defined(_WIN32)
        if (_putenv_s("PSPRECOMP_NO_CHAIN", "1") != 0)
#else
        if (setenv("PSPRECOMP_NO_CHAIN", "1", 1) != 0)
#endif
            throw psprecomp::Error("Could not disable AOT chaining");
    }
    return true;
}

} // namespace

bool GameSession::install_kernel() {
    std::uint32_t image_end = load_base;
    for (std::size_t index = 0; index < elf.segments().size(); ++index) {
        const auto &segment = elf.segments()[index];
        if (segment.type != 1u) continue;
        const auto end = static_cast<std::uint64_t>(elf.segment_runtime_address(index, load_base)) + segment.memory_size;
        if (end > 0x0A000000u) throw psprecomp::Error("Image exceeds PSP user RAM");
        image_end = std::max(image_end, static_cast<std::uint32_t>(end));
    }
    // Keep the start-up stack and its guard area outside guest allocations.
    kernel.install(runtime, image_end, 0x09FF6000u);
    return true;
}

GameSession::GameSession(const std::filesystem::path &executable, const std::filesystem::path &iso,
                         const SessionOptions &options)
    : elf(load_executable(executable)), disc(iso), chaining_configured(configure_chaining(options.count_every_dispatch)),
      savedata(options.memory_stick, [this] { return startup_hle.compiled_sdk_version(); }) {
    verify_disc(disc);
    const auto relocations = elf.load_and_relocate(runtime.memory(), load_base);
    if (relocations.invalid != 0u || relocations.unsupported != 0u)
        throw psprecomp::Error("Cannot execute an incompletely relocated image");
    const auto module = elf.find_module_info(runtime.memory(), load_base);
    if (!module || module->name != "SARU-P2") throw psprecomp::Error("Expected SARU-P2 module metadata");
    module_name_ = module->name;
    entry_ = elf.runtime_entry(load_base);
    psprecomp::register_generated_functions(runtime);

    startup_hle.install(runtime);
    kernel::register_early_threadman(kernel, runtime);
    kernel::register_sync(sync, runtime);
    kernel::register_interrupts(interrupts, runtime);
    umd::register_umd(umd, runtime, &kernel);
    io::register_io(io, runtime);
    ctrl::register_ctrl(controller, kernel, runtime);
    display::register_display(display, kernel, runtime);
    ge::register_ge(ge, kernel, runtime);
    rtc::register_rtc(rtc, runtime);
    modules::register_utility_modules(utility_modules, runtime);
    modules::register_module_manager(module_manager, runtime);
    audio::register_audio(audio, runtime);
    sas::register_sas(sas, runtime);
    audio::register_atrac(atrac, runtime);
    movie::register_psmf_player(psmf_player, runtime);
    utility::register_savedata(savedata, runtime);

    // Module start context: the launch path as the argument, a stack above the
    // image, and a return into the kernel's thread-exit stub so that the end of
    // module_start is an explicit scheduler transition, never guest PC zero.
    constexpr std::uint32_t stack = 0x09FFE000u;
    // Kernel scratch below the module image, out of reach of thread stacks.
    constexpr std::uint32_t arguments = 0x08100000u;
    constexpr std::string_view launch_path = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN";
    for (std::size_t i = 0; i < launch_path.size(); ++i)
        runtime.memory().store8(arguments + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(launch_path[i]));
    runtime.memory().store8(arguments + static_cast<std::uint32_t>(launch_path.size()), 0u);
    auto &cpu = runtime.cpu();
    cpu.set_gpr(4u, static_cast<std::uint32_t>(launch_path.size() + 1u));
    cpu.set_gpr(5u, arguments);
    cpu.set_gpr(28u, module->gp);
    cpu.set_gpr(29u, stack);
    cpu.set_gpr(31u, kernel::kThreadExitStub);
    kernel.begin_main_thread(cpu, "module_start", 0x20u);
}

void GameSession::run(std::uint64_t dispatch_limit) {
    runtime.run(entry_, dispatch_limit);
}

} // namespace sarusaru
