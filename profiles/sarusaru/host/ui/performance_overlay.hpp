#pragma once

// The performance overlay (shell/settings.hpp: PerformanceOverlay) as drawn
// over the running game, in the window's top-left corner like MangoHud's:
// the frame rate alone, or in detail with frame times, a frame-time graph,
// the game's own frame rate and speed, CPU and GPU time, and what the game
// runs on, so a screenshot of it says enough for a problem report. Its labels
// stay in English, whatever the launcher's language, for those reports.
#include "shell/performance_stats.hpp"
#include "shell/settings.hpp"

#include "imgui.h"

#include <cstdint>
#include <string>

namespace sarusaru::ui {

// What the game runs on and with, for the detailed overlay.
struct PerformanceInfo {
    std::string gpu;          // the device's name
    std::string api;          // "Vulkan 1.3 (MoltenVK)"
    std::string platform;     // "macOS"
    std::string version;      // the release
    std::uint32_t render_width{}, render_height{}, scale{};
    // The pictures a second the settings ask for: the display's 59.94 at
    // native, a cap, or 60 or 120 FPS motion. Frame rates below it are shown
    // yellow, and well below it red.
    float target_fps{};
    std::string frame_rate;   // the Frame Rate setting, as the launcher names it
};

// Into the foreground draw list of the current frame (Layer::present_over),
// over the whole window of `display` points. Off and MangoHud draw nothing.
void draw_performance_overlay(shell::PerformanceOverlay level, const shell::PerformanceStats &stats,
                              const PerformanceInfo &info, ImVec2 display);

} // namespace sarusaru::ui
