#pragma once
#include "save_state/session_access.hpp"

#include "kernel/early_kernel.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace sarusaru::modules {

inline constexpr std::uint32_t kUnknownModule = 0x8002012Eu;
inline constexpr std::uint32_t kModuleAlreadyStarted = 0x80020133u;
inline constexpr std::uint32_t kModuleNotStarted = 0x80020134u;

// ModuleMgrForUser for this game. Its two on-disc modules are Sony's media
// libraries (psmf.prx, libpsmfplayer.prx), whose exports the game imports and
// this profile provides by HLE, so "loading" one only creates a module ID.
// Any other module stops execution, naming its path.
class ModuleManager {
    friend struct sarusaru::save_state::SessionCodec;
public:
    explicit ModuleManager(kernel::EarlyKernel &kernel);

    // True for a module whose exports are provided by HLE.
    [[nodiscard]] static bool replaced_by_hle(std::string_view path) noexcept;
    // A module ID, or nullopt-equivalent negative error; stops for unknown modules.
    [[nodiscard]] std::int32_t load(psprecomp::Runtime &runtime, const std::string &path);
    [[nodiscard]] std::int32_t start(std::int32_t uid);
    [[nodiscard]] std::int32_t stop(std::int32_t uid);
    // Returns the module ID on success, as the PSP does.
    [[nodiscard]] std::int32_t unload(std::int32_t uid);
    [[nodiscard]] std::int32_t main_module() const noexcept { return main_uid_; }

private:
    struct Module { std::string path; bool started{}; };
    kernel::EarlyKernel *kernel_;
    std::int32_t main_uid_;
    std::map<std::int32_t, Module> modules_;
};

void register_module_manager(ModuleManager &modules, psprecomp::Runtime &runtime);

} // namespace sarusaru::modules
