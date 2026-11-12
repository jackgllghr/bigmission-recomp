#pragma once

// What the performance overlay (ui/performance_overlay.hpp) shows, gathered
// as the game runs: the pictures presented and the times between them, the
// game's own frames, the main thread's work at each vblank, the GPU's time,
// and how fast guest time runs against real time. The averages are over
// half a second, as MangoHud's are; the frame times are kept for a graph.
// Pure, so the tests drive it with made-up times.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace sarusaru::shell {

class PerformanceStats {
public:
    static constexpr std::size_t kHistory = 200;
    static constexpr std::uint64_t kWindowNs = 500'000'000u;

    struct Summary {
        float fps{};              // pictures presented a second
        float game_fps{};         // the game's own new frames a second
        float frame_ms{};         // the average time between pictures
        float worst_ms{};         // the longest of the recent frame times (the graph's)
        float one_percent_low{};  // the frame rate of the slowest 1% of the recent frames
        float cpu_ms{};           // the main thread's work a vblank: the game, its draws, presenting
        float gpu_ms{};           // the GPU's time a picture, where the device has timestamps
        float speed{};            // guest time against real time: 1 is full speed
        bool gpu_known{};
        bool valid{};             // a whole window has been measured
    };

    // A picture was handed to the display at `now_ns`.
    void presented(std::uint64_t now_ns) {
        if (last_present_ns_ != 0u && now_ns > last_present_ns_) {
            history_[next_] = static_cast<float>(static_cast<double>(now_ns - last_present_ns_) / 1e6);
            next_ = (next_ + 1u) % kHistory;
            filled_ = std::min(filled_ + 1u, kHistory);
        }
        last_present_ns_ = now_ns;
        ++presents_;
    }
    // A vblank's work on the main thread took `ns` (pacing sleeps excluded).
    void vblank_work(std::uint64_t ns) {
        work_ns_ += ns;
        ++vblanks_;
    }
    // At each vblank: the clocks, the game frames so far and the GPU's total
    // time so far (`gpu_known` false where the device cannot time itself).
    // Every half second the summary is worked out again.
    void sample(std::uint64_t now_ns, std::uint64_t guest_us, std::uint64_t game_frames, std::uint64_t gpu_total_ns,
                bool gpu_known) {
        if (window_start_ns_ == 0u) {
            start_window(now_ns, guest_us, game_frames, gpu_total_ns);
            return;
        }
        const std::uint64_t elapsed = now_ns - window_start_ns_;
        if (elapsed < kWindowNs) return;
        const double seconds = static_cast<double>(elapsed) / 1e9;
        Summary s;
        s.fps = static_cast<float>(static_cast<double>(presents_) / seconds);
        s.game_fps = static_cast<float>(static_cast<double>(game_frames - window_game_frames_) / seconds);
        s.frame_ms = presents_ != 0u ? static_cast<float>(static_cast<double>(elapsed) / 1e6 / static_cast<double>(presents_)) : 0.0f;
        s.cpu_ms = vblanks_ != 0u ? static_cast<float>(static_cast<double>(work_ns_) / 1e6 / static_cast<double>(vblanks_)) : 0.0f;
        s.gpu_known = gpu_known && gpu_total_ns >= window_gpu_ns_;
        if (s.gpu_known && presents_ != 0u)
            s.gpu_ms = static_cast<float>(static_cast<double>(gpu_total_ns - window_gpu_ns_) / 1e6 / static_cast<double>(presents_));
        s.speed = guest_us >= window_guest_us_
            ? static_cast<float>(static_cast<double>(guest_us - window_guest_us_) * 1e3 / static_cast<double>(elapsed)) : 0.0f;
        // The slowest 1% of the frame times in the graph, as a frame rate.
        if (filled_ != 0u) {
            std::array<float, kHistory> sorted{};
            std::copy_n(history_.begin(), filled_, sorted.begin());
            std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(filled_));
            s.worst_ms = sorted[filled_ - 1u];
            const std::size_t slowest = std::max<std::size_t>(1u, filled_ / 100u);
            float sum = 0.0f;
            for (std::size_t i = filled_ - slowest; i < filled_; ++i) sum += sorted[i];
            const float mean = sum / static_cast<float>(slowest);
            s.one_percent_low = mean > 0.0f ? 1000.0f / mean : 0.0f;
        }
        s.valid = true;
        summary_ = s;
        start_window(now_ns, guest_us, game_frames, gpu_total_ns);
    }
    // After a pause (the menu, a loaded state): the time it took is not a frame.
    void resume() {
        last_present_ns_ = 0u;
        window_start_ns_ = 0u;
    }

    [[nodiscard]] const Summary &summary() const { return summary_; }
    // The recent frame times in milliseconds, oldest first from `offset()` (a ring, as ImGui's plots take).
    [[nodiscard]] const std::array<float, kHistory> &frame_times() const { return history_; }
    [[nodiscard]] std::size_t count() const { return filled_; }
    [[nodiscard]] std::size_t offset() const { return filled_ < kHistory ? 0u : next_; }

private:
    void start_window(std::uint64_t now_ns, std::uint64_t guest_us, std::uint64_t game_frames, std::uint64_t gpu_ns) {
        window_start_ns_ = now_ns;
        window_guest_us_ = guest_us;
        window_game_frames_ = game_frames;
        window_gpu_ns_ = gpu_ns;
        presents_ = 0u;
        vblanks_ = 0u;
        work_ns_ = 0u;
    }

    std::array<float, kHistory> history_{};
    std::size_t next_{}, filled_{};
    std::uint64_t last_present_ns_{};
    std::uint64_t window_start_ns_{}, window_guest_us_{}, window_game_frames_{}, window_gpu_ns_{};
    std::uint64_t presents_{}, vblanks_{}, work_ns_{};
    Summary summary_{};
};

} // namespace sarusaru::shell
