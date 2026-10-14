#include "umd/umd_drive.hpp"

#include "psprecomp/common.hpp"

#include <optional>
#include <string>

namespace sarusaru::umd {
namespace {
bool valid_request(std::uint32_t mode, std::string_view alias) noexcept {
    return mode == 1u && alias == "disc0:";
}

std::optional<std::string> read_alias(psprecomp::Runtime &runtime, std::uint32_t address) {
    try {
        return runtime.memory().read_c_string(address, 16u);
    } catch (const psprecomp::Error &) {
        return std::nullopt;
    }
}
}

std::int32_t UmdDrive::activate(std::uint32_t mode, std::string_view alias) noexcept {
    if (!valid_request(mode, alias)) return static_cast<std::int32_t>(kInvalidArgument);
    activated_ = true;
    return 0;
}

std::int32_t UmdDrive::deactivate(std::uint32_t mode, std::string_view alias) noexcept {
    if (!valid_request(mode, alias)) return static_cast<std::int32_t>(kInvalidArgument);
    activated_ = false;
    return 0;
}

void register_umd(UmdDrive &drive, psprecomp::Runtime &runtime, kernel::EarlyKernel *kernel) {
    runtime.register_hle("sceUmdUser", 0x46EBB729u,  // sceUmdCheckMedium
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, 1u); });
    runtime.register_hle("sceUmdUser", 0x6B4A146Cu,  // sceUmdGetDriveStat
        [&drive](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, drive.drive_stat()); });
    runtime.register_hle("sceUmdUser", 0x20628E6Fu,  // sceUmdGetErrorStat
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, 0u); });
    const auto change = [&drive, kernel](bool activate) {
        return [&drive, kernel, activate](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto alias = read_alias(rt, ctx.gpr[5]);
            const auto result = !alias ? static_cast<std::int32_t>(kInvalidArgument)
                                : activate ? drive.activate(ctx.gpr[4], *alias) : drive.deactivate(ctx.gpr[4], *alias);
            if (kernel == nullptr) { ctx.set_gpr(2u, static_cast<std::uint32_t>(result)); return; }
            if (result == 0 && kernel->umd_callback() != 0) kernel->notify_callback(kernel->umd_callback(), drive.drive_stat());
            kernel->return_with_preemption(ctx, static_cast<std::uint32_t>(result));
        };
    };
    runtime.register_hle("sceUmdUser", 0xC6183D47u, change(true));   // sceUmdActivate
    runtime.register_hle("sceUmdUser", 0xE83742BAu, change(false));  // sceUmdDeactivate
    runtime.register_hle("sceUmdUser", 0x8EF08FCEu,  // sceUmdWaitDriveStat
        [&drive](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const std::uint32_t wanted = ctx.gpr[4];
            if ((drive.drive_stat() & wanted) != 0u) {
                ctx.set_gpr(2u, 0u);
                return;
            }
            rt.stop("sceUmdWaitDriveStat(" + psprecomp::hex32(wanted) + ") can never be satisfied: drive stat " +
                    psprecomp::hex32(drive.drive_stat()) + " has no asynchronous state changes");
        });
}

} // namespace sarusaru::umd
