#pragma once

// SARUSARU_TRACE_HITCHES=<ms> (20 when empty or 0) logs every frame whose
// work on the main thread took longer than that, split into the renderer's
// known costs. A frame is the time from the end of one vblank's pacing sleep
// to the start of the next, so the game's own CPU time is in it too: what the
// categories leave over is "other". Off, a timer costs one branch.
#include <SDL3/SDL_timer.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace sarusaru::shell::hitch {

enum Category : std::size_t {
    kPipeline,    // vkCreateGraphicsPipelines
    kDecode,      // gpu::decode_texture
    kHash,        // texture_key
    kPrompt,      // host button-prompt art
    kHost,        // translation lines drawn by the host (TexturePack::host_image)
    kJob,         // queueing a texture's art for a worker
    kArtWait,     // the frame's end waiting for art
    kFlush,       // VulkanContext::flush, submit and wait
    kFence,       // begin_frame waiting for a frame slot
    kAlloc,       // create_image / create_buffer (vkAllocateMemory)
    kUpload,      // copying texels into staging
    kAcquire,     // vkAcquireNextImageKHR (waits for a free swapchain image)
    kSubmit,      // vkQueueSubmit
    kPresent,     // vkQueuePresentKHR
    // Totals including the above, so not summed:
    kDraw,        // all of GeRenderer's draws
    kMiss,        // making new textures (GeRenderer::texture_view on a cache miss)
    kFrameEnd,    // GeRenderer::presentable, art put in place included
    kCategories,
};

inline constexpr const char *kNames[kCategories] = {"pipeline", "decode", "hash", "prompt", "host", "job", "art-wait",
                                                     "flush", "fence", "alloc", "upload", "acquire", "submit", "present", "draw-total",
                                                     "miss-total", "frame-end-total"};

struct State {
    bool enabled{};
    std::uint64_t threshold_ns{20'000'000u};
    std::uint64_t frame_start_ns{}, observer_ns{};
    std::array<std::uint64_t, kCategories> ns{};
    std::array<std::uint32_t, kCategories> count{};
    std::uint64_t frames{}, hitches{}, worst_ns{};
};

inline State &state() {
    static State instance = [] {
        State s;
        if (const char *value = std::getenv("SARUSARU_TRACE_HITCHES"); value != nullptr) {
            s.enabled = true;
            if (const double ms = std::strtod(value, nullptr); ms > 0.0) s.threshold_ns = static_cast<std::uint64_t>(ms * 1e6);
        }
        return s;
    }();
    return instance;
}

inline bool enabled() { return state().enabled; }

class Timer {
public:
    explicit Timer(Category category) : category_(category), start_(enabled() ? SDL_GetTicksNS() : 0u) {}
    ~Timer() { stop(); }
    // Ends the measurement early.
    void stop() {
        if (start_ == 0u) return;
        auto &s = state();
        s.ns[category_] += SDL_GetTicksNS() - start_;
        ++s.count[category_];
        start_ = 0u;
    }
    Timer(const Timer &) = delete;
    Timer &operator=(const Timer &) = delete;

private:
    Category category_;
    std::uint64_t start_;
};

// Starts a frame's measurement (after the pacing sleep).
inline void begin_frame() {
    auto &s = state();
    if (!s.enabled) return;
    s.frame_start_ns = SDL_GetTicksNS();
    s.observer_ns = 0u;
    s.ns = {};
    s.count = {};
}

// Marks the vblank's frame observer starting: the time before it is the
// guest's, draws included.
inline void enter_observer() {
    auto &s = state();
    if (s.enabled && s.observer_ns == 0u) s.observer_ns = SDL_GetTicksNS();
}

// Ends it (before the pacing sleep), logging it when over the threshold.
inline void end_frame(std::uint64_t guest_us) {
    auto &s = state();
    if (!s.enabled || s.frame_start_ns == 0u) return;
    const std::uint64_t total = SDL_GetTicksNS() - s.frame_start_ns;
    ++s.frames;
    if (total > s.worst_ns) s.worst_ns = total;
    if (total <= s.threshold_ns) return;
    ++s.hitches;
    std::uint64_t known = 0u;
    for (std::size_t i = 0; i < kDraw; ++i) known += s.ns[i];
    std::fprintf(stderr, "hitch at %.3f s: %.1f ms", static_cast<double>(guest_us) / 1e6, static_cast<double>(total) / 1e6);
    for (std::size_t i = 0; i < kCategories; ++i)
        if (s.count[i] != 0u)
            std::fprintf(stderr, " %s=%.1fms/%u", kNames[i], static_cast<double>(s.ns[i]) / 1e6, s.count[i]);
    std::fprintf(stderr, " other=%.1fms", static_cast<double>(total > known ? total - known : 0u) / 1e6);
    if (s.observer_ns > s.frame_start_ns)
        std::fprintf(stderr, " (guest=%.1fms incl. draws)", static_cast<double>(s.observer_ns - s.frame_start_ns) / 1e6);
    std::fputc('\n', stderr);
}

inline void report() {
    auto &s = state();
    if (!s.enabled) return;
    std::fprintf(stderr, "hitches: %llu of %llu frames over %.1f ms, worst %.1f ms\n",
                 static_cast<unsigned long long>(s.hitches), static_cast<unsigned long long>(s.frames),
                 static_cast<double>(s.threshold_ns) / 1e6, static_cast<double>(s.worst_ns) / 1e6);
}

} // namespace sarusaru::shell::hitch
