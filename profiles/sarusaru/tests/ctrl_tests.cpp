#include "ctrl/controller.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace sarusaru::ctrl;
    Controller controller;
    require(controller.set_sampling_mode(1u) == 0 && controller.set_sampling_mode(0u) == 1 &&
                controller.sampling_mode() == 0u,
            "sampling mode did not return previous mode");
    require(static_cast<std::uint32_t>(controller.set_sampling_mode(2u)) == kInvalidMode && controller.sampling_mode() == 0u,
            "invalid sampling mode accepted");
    require(controller.set_sampling_cycle(5555u) == 0 && controller.set_sampling_cycle(0u) == 5555,
            "sampling cycle did not return previous cycle");
    require(static_cast<std::uint32_t>(controller.set_sampling_cycle(5554u)) == kInvalidValue &&
                static_cast<std::uint32_t>(controller.set_sampling_cycle(20001u)) == kInvalidValue,
            "out-of-range sampling cycle accepted");
    require(static_cast<std::uint32_t>(controller.set_idle_cancel_threshold(129, 0)) == kInvalidValue &&
                controller.idle_reset() == -1,
            "out-of-range idle threshold accepted");

    psprecomp::Runtime runtime;
    register_ctrl(controller, runtime);
    auto &cpu = runtime.cpu();
    cpu.set_gpr(4u, 1u);
    runtime.invoke_import("sceCtrl", 0x1F4011E6u, cpu);
    require(cpu.gpr[2] == 0u && controller.sampling_mode() == 1u, "HLE sampling mode failed");
    cpu.set_gpr(4u, 0xFFFFFFFFu); cpu.set_gpr(5u, 0xFFFFFFFFu);
    runtime.invoke_import("sceCtrl", 0xA7144800u, cpu);
    require(cpu.gpr[2] == 0u && controller.idle_reset() == -1 && controller.idle_back() == -1, "HLE idle threshold failed");
    cpu.set_gpr(4u, 16u); cpu.set_gpr(5u, 128u);
    runtime.invoke_import("sceCtrl", 0xA7144800u, cpu);
    require(cpu.gpr[2] == 0u && controller.idle_reset() == 16 && controller.idle_back() == 128, "idle threshold not stored");
    return 0;
}
