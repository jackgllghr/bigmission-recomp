#include "modules/utility_modules.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace sarusaru::modules;
    UtilityModules modules;
    require(modules.load(0x300u) == 0 && modules.loaded(0x300u), "AV codec load");
    require(static_cast<std::uint32_t>(modules.load(0x300u)) == kModuleAlreadyLoaded, "double load accepted");
    require(static_cast<std::uint32_t>(modules.load(0x308u)) == kBadModuleId &&
                static_cast<std::uint32_t>(modules.load(0x0FFu)) == kBadModuleId,
            "unknown module accepted");
    require(static_cast<std::uint32_t>(modules.unload(0x301u)) == kModuleNotLoaded, "unload of unloaded module");

    psprecomp::Runtime runtime;
    register_utility_modules(modules, runtime);
    auto &cpu = runtime.cpu();
    cpu.set_gpr(4u, 0x301u);
    runtime.invoke_import("sceUtility", 0x2A2B3DE0u, cpu);
    require(cpu.gpr[2] == 0u && modules.loaded(0x301u), "HLE load");
    runtime.invoke_import("sceUtility", 0xE49BFE92u, cpu);
    require(cpu.gpr[2] == 0u && !modules.loaded(0x301u), "HLE unload");
    return 0;
}
