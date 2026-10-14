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
}

} // namespace sarusaru
