#pragma once

#include "umd/disc_image.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <string_view>

namespace sarusaru::umd {

// sceUmd drive-status bits (pspumd.h).
inline constexpr std::uint32_t kStatNotPresent = 0x01u;
inline constexpr std::uint32_t kStatPresent = 0x02u;
inline constexpr std::uint32_t kStatChanged = 0x04u;
inline constexpr std::uint32_t kStatInitializing = 0x08u;
inline constexpr std::uint32_t kStatInitialized = 0x10u;
inline constexpr std::uint32_t kStatReady = 0x20u;
inline constexpr std::uint32_t kInvalidArgument = 0x80010016u;

// The drive always holds the user's verified disc; ejecting and swapping are
// not modeled. Activation is synchronous, so a wait either is satisfied at
// once or can never be, and the latter stops execution explicitly.
class UmdDrive {
public:
    explicit UmdDrive(DiscImage &disc) noexcept : disc_(&disc) {}

    [[nodiscard]] DiscImage &disc() noexcept { return *disc_; }
    [[nodiscard]] bool activated() const noexcept { return activated_; }
    [[nodiscard]] std::uint32_t drive_stat() const noexcept {
        return kStatPresent | kStatInitialized | (activated_ ? kStatReady : 0u);
    }
    [[nodiscard]] std::int32_t activate(std::uint32_t mode, std::string_view alias) noexcept;
    [[nodiscard]] std::int32_t deactivate(std::uint32_t mode, std::string_view alias) noexcept;

private:
    DiscImage *disc_;
    bool activated_{};
};

void register_umd(UmdDrive &drive, psprecomp::Runtime &runtime);

} // namespace sarusaru::umd
