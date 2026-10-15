#pragma once

#include "save_state/machine.hpp"
#include "save_state/ppsspp_objects.hpp"
#include "psprecomp/runtime.hpp"

namespace sarusaru::save_state {
struct PspImportBinding {
    std::string library;
    std::uint32_t nid{};
    bool operator==(const PspImportBinding &) const = default;
};
// Resolve PPSSPP's process-local syscall numbers from the imported symbols
// and actual stub instructions in detached guest RAM. Never assume PPSSPP's
// compiled-in module/function table order matches this runtime's import slots.
class PspSyscallTable {
public:
    static PspSyscallTable decode(const MachinePrefix &machine, const PspKernelObjects &objects);
    [[nodiscard]] const std::map<std::uint32_t, PspImportBinding> &imports() const noexcept { return imports_; }
    // Copies only value bindings into the destination runtime. Unknown codes,
    // including kernel synthetic calls, stop explicitly until their matching
    // kernel/HLE continuation adapter is installed.
    void install(psprecomp::Runtime &runtime) const;
private:
    std::map<std::uint32_t, PspImportBinding> imports_;
};
} // namespace sarusaru::save_state
