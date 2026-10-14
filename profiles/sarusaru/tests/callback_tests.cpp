#include "kernel/early_kernel.hpp"
#include "umd/umd_drive.hpp"
#include "synthetic_iso.hpp"

#include <cstdlib>
#include <iostream>

namespace {
using namespace sarusaru::kernel;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void call(psprecomp::Runtime &runtime, const char *library, std::uint32_t nid, std::uint32_t return_address,
          std::initializer_list<std::uint32_t> arguments = {}) {
    auto &cpu = runtime.cpu();
    std::uint32_t reg = 4u;
    for (const auto argument : arguments) cpu.set_gpr(reg++, argument);
    cpu.pc = 0x08A00000u;
    cpu.set_gpr(31u, return_address);
    runtime.invoke_import(library, nid, cpu);
}

constexpr std::uint32_t kCheckCallback = 0x349D6D6Cu, kDelayCB = 0x68DA9E36u, kDelay = 0xCEADEB47u;
constexpr std::uint32_t kCallbackFunction = 0x08806000u;
}

int main() {
    psprecomp::Runtime runtime;
    EarlyKernel kernel;
    kernel.install(runtime, 0x08804000u, 0x0A000000u);
    auto &cpu = runtime.cpu();
    cpu.pc = 0x08804000u;
    cpu.set_gpr(29u, 0x09FF0000u);
    kernel.begin_main_thread(cpu, "main", 0x20u);
    const auto main_uid = kernel.current_uid();
    register_early_threadman(kernel, runtime);
    const auto callback = kernel.create_callback("cb", kCallbackFunction, 0xC0u);

    call(runtime, "ThreadManForUser", kCheckCallback, 0x08804010u);
    require(cpu.gpr[2] == 0u, "check with nothing notified");

    // CheckCallback runs a notified callback on the calling thread, below its frame.
    kernel.notify_callback(callback, 5u);
    call(runtime, "ThreadManForUser", kCheckCallback, 0x08804100u);
    require(cpu.pc == kCallbackFunction && cpu.gpr[4] == 1u && cpu.gpr[5] == 5u && cpu.gpr[6] == 0xC0u &&
                cpu.gpr[31] == kGuestCallReturnStub && cpu.gpr[29] < 0x09FF0000u && kernel.current_uid() == main_uid,
            "callback not entered");
    cpu.set_gpr(2u, 0u);
    kernel.guest_call_returned(cpu);
    require(cpu.pc == 0x08804100u && cpu.gpr[2] == 1u && cpu.gpr[29] == 0x09FF0000u && kernel.callback(callback) != nullptr,
            "check did not resume after callback");

    // A notification interrupts a *CB wait; the wait then resumes with its deadline.
    const auto worker = kernel.create_thread("worker", 0x08805000u, 0x30u, 0x400u, 0u, 0u);
    require(kernel.start_thread(cpu, worker, 0u, 0u) == 0, "worker start");
    call(runtime, "ThreadManForUser", kDelayCB, 0x08804200u, {1000u});
    require(kernel.current_uid() == worker, "CB delay did not block");
    kernel.notify_callback(callback, 7u);
    kernel.return_with_preemption(cpu, 0u);
    require(kernel.current_uid() == main_uid && cpu.pc == kCallbackFunction && cpu.gpr[5] == 7u,
            "notification did not run the callback in the waiting thread");
    cpu.set_gpr(2u, 1u);  // Nonzero return deletes the callback.
    kernel.guest_call_returned(cpu);
    require(kernel.current_uid() == worker && kernel.callback(callback) == nullptr, "delay did not resume waiting");
    call(runtime, "ThreadManForUser", kDelay, 0x08805100u, {5000u});
    require(kernel.current_uid() == main_uid && kernel.now_us() == 1000u && cpu.pc == 0x08804200u && cpu.gpr[2] == 0u,
            "resumed delay did not keep its deadline");

    // UMD activation notifies the registered UMD callback with the drive status.
    const auto umd_callback = kernel.create_callback("umd", kCallbackFunction, 0u);
    require(kernel.register_umd_callback(umd_callback) == 0, "umd callback registration");
    const auto path = sarusaru::test::write_image(sarusaru::test::synthetic_image(), "sarusaru_callback_tests.iso");
    {
        sarusaru::umd::DiscImage disc(path);
        sarusaru::umd::UmdDrive drive(disc);
        sarusaru::umd::register_umd(drive, runtime, &kernel);
        for (std::size_t i = 0; i < 7u; ++i) runtime.memory().store8(0x08807000u + static_cast<std::uint32_t>(i), "disc0:"[i]);
        call(runtime, "sceUmdUser", 0xC6183D47u, 0x08804300u, {1u, 0x08807000u});
        require(cpu.gpr[2] == 0u && kernel.callback(umd_callback)->notify_count == 1u &&
                    kernel.callback(umd_callback)->notify_argument == 0x32u,
                "activation did not notify the UMD callback");
    }
    std::filesystem::remove(path);
    call(runtime, "ThreadManForUser", 0xEDBA5844u, 0x08804400u, {static_cast<std::uint32_t>(umd_callback)});
    require(cpu.gpr[2] == 0u && kernel.umd_callback() == 0 && kernel.delete_callback(umd_callback) ==
                static_cast<std::int32_t>(kUnknownCallback),
            "callback deletion");
    return 0;
}
