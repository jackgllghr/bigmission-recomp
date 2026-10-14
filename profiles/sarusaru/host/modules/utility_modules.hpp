#pragma once

#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <set>

namespace sarusaru::modules {

inline constexpr std::uint32_t kBadModuleId = 0x80111101u;
inline constexpr std::uint32_t kModuleAlreadyLoaded = 0x80111102u;
inline constexpr std::uint32_t kModuleNotLoaded = 0x80111103u;

// sceUtilityLoadModule/UnloadModule. The system libraries these load are
// provided by HLE at their import interface, so loading only records state;
// a library's functions still stop execution until each is modeled.
class UtilityModules {
public:
    [[nodiscard]] static bool known(std::uint32_t id) noexcept;
    [[nodiscard]] std::int32_t load(std::uint32_t id);
    [[nodiscard]] std::int32_t unload(std::uint32_t id);
    [[nodiscard]] bool loaded(std::uint32_t id) const noexcept { return loaded_.contains(id); }

private:
    std::set<std::uint32_t> loaded_;
};

void register_utility_modules(UtilityModules &modules, psprecomp::Runtime &runtime);

} // namespace sarusaru::modules
