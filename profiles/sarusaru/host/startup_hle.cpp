#include "startup_hle.hpp"

namespace sarusaru {

void StartupHle::install(psprecomp::Runtime &runtime) {
    runtime.register_hle("SysMemUserForUser", 0x7591C7DBu,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            compiled_sdk_version_ = ctx.gpr[4];
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("SysMemUserForUser", 0xF77D77CBu,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            compiler_version_ = ctx.gpr[4];
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("UtilsForUser", 0x6AD345D7u,  // sceKernelSetGPO
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            gpo_ = ctx.gpr[4] & 0xFFu;
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("sceSuspendForUser", 0x090CCB3Fu,  // sceKernelPowerTick(type)
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ++power_ticks_;
            ctx.set_gpr(2u, 0u);
        });
    const auto cache_writeback = [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, 0u); };
    runtime.register_hle("UtilsForUser", 0x79D1C3FAu, cache_writeback);  // sceKernelDcacheWritebackAll
    runtime.register_hle("UtilsForUser", 0xB435DEC5u, cache_writeback);  // sceKernelDcacheWritebackInvalidateAll
    runtime.register_hle("UtilsForUser", 0x3EE30821u, cache_writeback);  // sceKernelDcacheWritebackRange
}

} // namespace sarusaru
