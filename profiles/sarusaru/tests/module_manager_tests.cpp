#include "modules/module_manager.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace sarusaru::modules;
    psprecomp::Runtime runtime;
    sarusaru::kernel::EarlyKernel kernel;
    kernel.install(runtime, 0x08804000u, 0x0A000000u);
    ModuleManager modules(kernel);
    register_module_manager(modules, runtime);
    auto &cpu = runtime.cpu();

    require(ModuleManager::replaced_by_hle("disc0:/PSP_GAME/USRDIR/module/PSMF.PRX") &&
                !ModuleManager::replaced_by_hle("disc0:/PSP_GAME/USRDIR/module/other.prx"),
            "HLE-replaced modules");
    constexpr std::uint32_t path = 0x08807000u, status = 0x08807100u;
    const std::string name = "disc0:/PSP_GAME/USRDIR/module/libpsmfplayer.prx";
    for (std::size_t i = 0; i <= name.size(); ++i) runtime.memory().store8(path + static_cast<std::uint32_t>(i), i < name.size() ? name[i] : 0);
    cpu.set_gpr(4u, path);
    runtime.invoke_import("ModuleMgrForUser", 0x977DE386u, cpu);
    const auto uid = cpu.gpr[2];
    require(static_cast<std::int32_t>(uid) > 0 && uid != static_cast<std::uint32_t>(modules.main_module()), "load");
    runtime.memory().store32(status, 0xFFFFFFFFu);
    cpu.set_gpr(4u, uid); cpu.set_gpr(7u, status);
    runtime.invoke_import("ModuleMgrForUser", 0x50F0C1ECu, cpu);
    require(cpu.gpr[2] == uid && runtime.memory().load32(status) == 0u, "start");
    require(static_cast<std::uint32_t>(modules.start(static_cast<std::int32_t>(uid))) == kModuleAlreadyStarted, "double start");
    require(modules.stop(static_cast<std::int32_t>(uid)) == 0 &&
                modules.unload(static_cast<std::int32_t>(uid)) == static_cast<std::int32_t>(uid) &&
                static_cast<std::uint32_t>(modules.unload(static_cast<std::int32_t>(uid))) == kUnknownModule,
            "stop and unload");
    runtime.invoke_import("ModuleMgrForUser", 0xF0A26395u, cpu);
    require(cpu.gpr[2] == static_cast<std::uint32_t>(modules.main_module()), "main module ID");

    const std::string other = "disc0:/other.prx";
    for (std::size_t i = 0; i <= other.size(); ++i) runtime.memory().store8(path + static_cast<std::uint32_t>(i), i < other.size() ? other[i] : 0);
    cpu.set_gpr(4u, path);
    runtime.invoke_import("ModuleMgrForUser", 0x977DE386u, cpu);
    require(runtime.stopped() && runtime.stop_reason().find("other.prx") != std::string::npos, "unknown module stops");
    return 0;
}
