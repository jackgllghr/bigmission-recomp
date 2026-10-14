#pragma once

// Motion interpolation between two consecutive game frames, at the level of
// the GE's draw calls.
//
// The game's movement, animation and script timers advance by fixed amounts
// per game frame, so running its update loop faster speeds the game up. The
// game therefore keeps its native cadence (two vblanks per frame), and the
// renderer draws an extra picture between two of its frames: every draw of the
// newer frame is matched with the same draw of the older one, and its
// matrices, vertex positions, normals and colours are blended. Nothing here
// touches guest memory or game state, so gameplay timing stays native.
//
// Draws are matched by where their vertices come from and what they draw
// with; among several candidates (instances of one mesh), the one whose first
// vertex lies nearest wins. A draw that has no match, or moved too far to be
// the same motion (a camera cut, a teleport, a recycled sprite), is drawn as
// it is in the newer frame.
#include "gpu/ge_state.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sarusaru::gpu {

class FrameInterpolator {
public:
    struct Stats {
        std::uint64_t matched{};   // blended with their previous-frame draw
        std::uint64_t unmatched{}; // no previous-frame draw recognised
        std::uint64_t snapped{};   // matched, but moved too far to blend
    };

    // The draws recorded since the last call become the previous frame.
    void end_frame();
    // Records `call` as a draw of the current frame and writes to `out` the
    // draw at `t` between its previous-frame match (0) and itself (1).
    // Returns whether `out` was blended; otherwise it is a copy of `call`.
    bool interpolate(const DrawCall &call, float t, DrawCall &out);
    // Forgets both frames, as after a discontinuity the caller knows of.
    void reset();

    [[nodiscard]] const Stats &stats() const noexcept { return stats_; }

private:
    struct Record {
        std::uint64_t exact_key{};
        std::uint64_t loose_key{};
        bool through{};
        std::array<float, 16> world{}, view{}, projection{};
        float offset_u{}, offset_v{};
        std::uint32_t material_color{};
        std::vector<Vertex> vertices;
        std::array<float, 3> anchor{}; // the first vertex in view (or screen) space
        bool used{};
    };
    struct Candidates {
        std::vector<std::uint32_t> records;
        std::size_t first_unused{};
    };

    [[nodiscard]] Record *match(const Record &current);
    Record *best_of(Candidates &candidates, const Record &current);

    std::vector<Record> previous_, current_;
    std::unordered_map<std::uint64_t, Candidates> exact_, loose_;
    Stats stats_;
};

} // namespace sarusaru::gpu
