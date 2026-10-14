#include "startup_hle.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    psprecomp::Runtime runtime;
    sarusaru::StartupHle hle;
    hle.install(runtime);
    auto &cpu = runtime.cpu();

    cpu.set_gpr(4u, 0x06060110u);
    cpu.set_gpr(2u, 0xFFFFFFFFu);
    runtime.invoke_import("SysMemUserForUser", 0x7591C7DBu, cpu);
    require(!runtime.stopped(), "compiled SDK import stopped runtime");
    require(hle.compiled_sdk_version() == 0x06060110u, "compiled SDK version was not retained");
    require(cpu.gpr[2] == 0u, "compiled SDK import did not return zero");

    cpu.set_gpr(4u, 0x12345678u);
    cpu.set_gpr(2u, 0xFFFFFFFFu);
    runtime.invoke_import("SysMemUserForUser", 0xF77D77CBu, cpu);
    require(!runtime.stopped(), "compiler-version import stopped runtime");
    require(hle.compiler_version() == 0x12345678u, "compiler version was not retained");
    require(cpu.gpr[2] == 0u, "compiler-version import did not return zero");
    return 0;
}
