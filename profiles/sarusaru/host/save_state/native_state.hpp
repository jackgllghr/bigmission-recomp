#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sarusaru { class GameSession; }
namespace sarusaru::save_state {
[[nodiscard]] inline std::filesystem::path quick_state_path(const std::filesystem::path &settings_path) {
    return settings_path.parent_path() / "states" / "UCJS10060.quick.bmstate";
}

// Native quick states belong to this runtime ABI and game-text configuration.
// All calls require a stopped runtime or its outer dispatch hook.
class NativeState {
public:
    static void save(GameSession &, const std::filesystem::path &, const std::string &configuration,
                     std::span<const std::uint8_t> renderer);
    // The prepared snapshot is bound to this destination session, which must
    // outlive it; apply() rejects another destination.
    static NativeState prepare(GameSession &, const std::filesystem::path &, const std::string &configuration);
    std::span<const std::uint8_t> renderer() const;
    void apply(GameSession &) &&;
    ~NativeState();
    NativeState(NativeState &&) noexcept;
    NativeState &operator=(NativeState &&) noexcept;
private:
    struct Data;
    explicit NativeState(std::unique_ptr<Data>);
    std::unique_ptr<Data> data_;
    friend struct SessionCodec;
};
}
