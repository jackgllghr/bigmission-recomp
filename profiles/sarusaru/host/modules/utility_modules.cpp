#include "modules/utility_modules.hpp"

namespace sarusaru::modules {

bool UtilityModules::known(std::uint32_t id) noexcept {
    // psputility_modules.h: network, USB, audio/video, NP and IrDA modules.
    return (id >= 0x0100u && id <= 0x0106u) || (id >= 0x0200u && id <= 0x0203u) ||
           (id >= 0x0300u && id <= 0x0307u) || (id >= 0x0400u && id <= 0x0402u) || id == 0x0500u || id == 0x0600u;
}

std::int32_t UtilityModules::load(std::uint32_t id) {
    if (!known(id)) return static_cast<std::int32_t>(kBadModuleId);
    if (!loaded_.insert(id).second) return static_cast<std::int32_t>(kModuleAlreadyLoaded);
    return 0;
}

std::int32_t UtilityModules::unload(std::uint32_t id) {
    if (!known(id)) return static_cast<std::int32_t>(kBadModuleId);
    if (loaded_.erase(id) == 0u) return static_cast<std::int32_t>(kModuleNotLoaded);
    return 0;
}

void register_utility_modules(UtilityModules &modules, psprecomp::Runtime &runtime) {
    runtime.register_hle("sceUtility", 0x2A2B3DE0u,  // sceUtilityLoadModule
        [&modules](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(modules.load(ctx.gpr[4])));
        });
    runtime.register_hle("sceUtility", 0xE49BFE92u,  // sceUtilityUnloadModule
        [&modules](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(modules.unload(ctx.gpr[4])));
        });
}

} // namespace sarusaru::modules
