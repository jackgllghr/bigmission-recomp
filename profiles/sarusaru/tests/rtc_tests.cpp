#include "rtc/rtc.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace sarusaru::rtc;
    const auto epoch = civil_from_unix_us(0);
    require(epoch.year == 1970 && epoch.month == 1 && epoch.day == 1 && epoch.hour == 0, "Unix epoch");
    // 2008-02-29 23:59:59.5 (a leap day, in the game's era).
    const auto leap = civil_from_unix_us(1204329599'500000);
    require(leap.year == 2008 && leap.month == 2 && leap.day == 29 && leap.hour == 23 && leap.minute == 59 &&
                leap.second == 59 && leap.microsecond == 500000u,
            "leap day");
    const auto before = civil_from_unix_us(-1);
    require(before.year == 1969 && before.month == 12 && before.day == 31 && before.second == 59 &&
                before.microsecond == 999999u,
            "time before the epoch");

    // The guest clock advances with virtual time.
    psprecomp::Runtime runtime;
    sarusaru::kernel::EarlyKernel kernel;
    kernel.install(runtime, 0x08804000u, 0x0A000000u);
    auto &cpu = runtime.cpu();
    kernel.begin_main_thread(cpu, "main", 0x20u);
    Rtc rtc(kernel, 1204329599'500000);
    register_rtc(rtc, runtime);
    sarusaru::kernel::register_early_threadman(kernel, runtime);
    cpu.set_gpr(4u, 600000u);
    cpu.pc = 0x08A00000u; cpu.set_gpr(31u, 0x08804100u);
    runtime.invoke_import("ThreadManForUser", 0xCEADEB47u, cpu);
    constexpr std::uint32_t out = 0x08807000u;
    cpu.set_gpr(4u, out);
    runtime.invoke_import("sceRtc", 0xE7C27D1Bu, cpu);
    auto &memory = runtime.memory();
    require(cpu.gpr[2] == 0u && memory.load16(out) == 2008u && memory.load16(out + 2u) == 3u && memory.load16(out + 4u) == 1u &&
                memory.load16(out + 6u) == 0u && memory.load16(out + 10u) == 0u && memory.load32(out + 12u) == 100000u,
            "guest clock did not follow virtual time");
    cpu.set_gpr(4u, 0u);
    runtime.invoke_import("sceRtc", 0xE7C27D1Bu, cpu);
    require(cpu.gpr[2] == kInvalidPointer, "null output accepted");
    return 0;
}
