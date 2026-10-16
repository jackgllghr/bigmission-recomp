#include "session.hpp"

#include "game_disc.hpp"
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

constexpr DiscIdentity kIdentity{disc_id, executable_size, executable_sha256};

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

DiscIdentity supported_disc() { return kIdentity; }

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

GameSession::GameSession(const std::filesystem::path &iso, const SessionOptions &options)
    : GameSession({}, iso, options) {}

GameSession::GameSession(const std::filesystem::path &executable, const std::filesystem::path &iso,
                         const SessionOptions &options)
    : game_module(options.game_module.empty() ? GameModule() : GameModule(options.game_module)),
      elf(executable.empty() ? executable_from_disc(iso, kIdentity) : load_executable(executable)), disc(iso), chaining_configured(configure_chaining(options.count_every_dispatch)),
      frame_rate(options.experimental_game_frame_rate), intro_logos(options.skip_intro_logos),
      savedata(options.memory_stick, [this] { return startup_hle.compiled_sdk_version(); }) {
    verify_disc(disc, kIdentity);
    const auto relocations = elf.load_and_relocate(runtime.memory(), load_base);
    if (relocations.invalid != 0u || relocations.unsupported != 0u)
        throw psprecomp::Error("Cannot execute an incompletely relocated image");
    const auto module = elf.find_module_info(runtime.memory(), load_base);
    if (!module || module->name != "SARU-P2") throw psprecomp::Error("Expected SARU-P2 module metadata");
    module_name_ = module->name;
    entry_ = elf.runtime_entry(load_base);
    game_module.register_functions(runtime);

    startup_hle.install(runtime);
    kernel::register_early_threadman(kernel, runtime);
    kernel::register_sync(sync, runtime);
    kernel::register_interrupts(interrupts, runtime);
    umd::register_umd(umd, runtime, &kernel);
    io::register_io(io, runtime);
    ctrl::register_ctrl(controller, kernel, runtime);
    display::VcountHook vcount_hook;
    display::VblankWaitHook vblank_wait_hook;
    if (frame_rate.enabled()) {
        vcount_hook = [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) { frame_rate.on_vcount(rt, ctx); };
        vblank_wait_hook = [this](psprecomp::Runtime &rt, const psprecomp::AllegrexContext &ctx, std::uint64_t now) {
            return frame_rate.wait_deadline(rt, ctx, now);
        };
    }
    display::register_display(display, kernel, runtime, std::move(vcount_hook), std::move(vblank_wait_hook));
    track_gameplay_camera_ = options.track_gameplay_camera;
    camera_control_ = options.camera_control;
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
    observed_session_ = this;
    psprecomp::set_runtime_pre_chained_call_hook(&observe_call);
    psprecomp::set_runtime_pre_dispatch_hook(&observe_dispatch);
}

GameSession *GameSession::observed_session_ = nullptr;

GameSession::~GameSession() {
    if (observed_session_ != this) return;
    observed_session_ = nullptr;
    psprecomp::set_runtime_pre_chained_call_hook(nullptr);
    psprecomp::set_runtime_pre_dispatch_hook(nullptr);
}

void GameSession::observe_call(psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx,
                               std::uint32_t pc, std::uint32_t) {
    if (auto *session = observed_session_) {
        session->intro_logos.on_call(pc, rt.memory(), ctx, session->controller, session->kernel.now_us());
        if (session->camera_control_) {
            session->orbit_camera.on_call(pc, rt.memory(), ctx, session->kernel.now_us());
            session->camera_fov.on_call(pc, rt.memory(), ctx);
        }
        if (session->track_gameplay_camera_) {
            session->gameplay_camera.on_call(pc);
        } else if (!session->camera_control_ && session->intro_logos.finished()) {
            // Restore direct chaining after startup when no camera observer is needed.
            psprecomp::set_runtime_pre_chained_call_hook(nullptr);
            psprecomp::set_runtime_pre_dispatch_hook(nullptr);
        }
    }
}

void GameSession::observe_dispatch(psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx,
                                   std::uint32_t pc, std::int32_t) {
    observe_call(rt, ctx, pc, 0u);
}

void GameSession::restore_observers() {
    observed_session_ = this;
    psprecomp::set_runtime_pre_chained_call_hook(&observe_call);
    psprecomp::set_runtime_pre_dispatch_hook(&observe_dispatch);
}

void GameSession::run(std::uint64_t dispatch_limit) {
    runtime.run(entry_, dispatch_limit);
}

} // namespace sarusaru
