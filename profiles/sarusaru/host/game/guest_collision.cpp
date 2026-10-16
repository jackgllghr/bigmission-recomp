#include "game/guest_collision.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>

namespace sarusaru::game {
namespace {
// The query's scratch: the collision walker's state and its per-query result
// slots. Saved and restored so a camera query leaves the game's state as it was.
constexpr std::array<std::array<std::uint32_t, 2>, 2> kScratch{{{0x08A80580u, 0x08A80620u}, {0x08AA5700u, 0x08AA5A00u}}};
// Stack borrowed below the hooked call's frame; the sweep nests about 1 KiB.
constexpr std::uint32_t kStackBytes = 0x1000u;
// A return address outside the image: the sweep is done when it reaches it.
constexpr std::uint32_t kReturn = 0x08800004u;
constexpr unsigned kMaxDispatches = 20000u;
constexpr std::uint32_t kFractionOffset = 100u;

struct Saved {
    std::uint32_t begin;
    std::vector<std::uint32_t> words;
};
Saved save(const psprecomp::GuestMemory &memory, std::uint32_t begin, std::uint32_t end) {
    Saved saved{begin, {}};
    saved.words.reserve((end - begin) / 4u);
    for (auto address = begin; address < end; address += 4u) saved.words.push_back(memory.load32(address));
    return saved;
}
void restore(psprecomp::GuestMemory &memory, const Saved &saved) {
    auto address = saved.begin;
    for (const auto word : saved.words) memory.store32(address, word), address += 4u;
}
void store(psprecomp::GuestMemory &memory, std::uint32_t address, const std::array<float, 3> &value, float w) {
    for (unsigned i = 0; i < 3u; ++i) memory.store32(address + i * 4u, std::bit_cast<std::uint32_t>(value[i]));
    memory.store32(address + 12u, std::bit_cast<std::uint32_t>(w));
}
}

float GuestCollision::sweep(const psprecomp::AllegrexContext &caller, const std::array<float, 3> &start,
                            const std::array<float, 3> &delta, float radius) {
    auto &memory = runtime_.memory();
    const std::uint32_t stack_top = caller.gpr[29] & ~0xFu;
    if (active_ || stack_top < kStackBytes || !memory.contains(stack_top - kStackBytes, kStackBytes)) return 1.0f;
    for (const auto &[begin, end] : kScratch)
        if (!memory.contains(begin, end - begin)) return 1.0f;

    std::vector<Saved> saved;
    saved.push_back(save(memory, stack_top - kStackBytes, stack_top));
    for (const auto &[begin, end] : kScratch) saved.push_back(save(memory, begin, end));
    struct Restore {
        psprecomp::GuestMemory &memory;
        const std::vector<Saved> &saved;
        bool &active;
        ~Restore() {
            for (const auto &range : saved) restore(memory, range);
            active = false;
        }
    } restore_on_exit{memory, saved, active_};
    active_ = true;

    // Arguments sit at the top of the borrowed stack; the sweep's frames go below.
    const std::uint32_t base = stack_top - 0x100u, start_address = base, delta_address = base + 0x10u,
                        result = base + 0x20u;
    store(memory, start_address, start, 1.0f);
    store(memory, delta_address, delta, 0.0f);
    for (std::uint32_t offset = 0; offset < 0x80u; offset += 4u) memory.store32(result + offset, 0u);

    psprecomp::AllegrexContext ctx = caller;
    ctx.gpr[4] = kPlayerSolids;
    ctx.gpr[5] = start_address;
    ctx.gpr[6] = delta_address;
    ctx.gpr[7] = result;
    ctx.fpr[12] = radius;
    ctx.gpr[29] = base;
    ctx.gpr[31] = kReturn;
    ctx.pc = kSphereSweep;
    // A generated unit returns at each call it cannot chain into another
    // unit; continue from there, as the outer dispatcher would.
    unsigned dispatches = 0;
    while (ctx.pc != kReturn) {
        if (++dispatches > kMaxDispatches || !runtime_.invoke_isolated_aot(ctx.pc, ctx)) return 1.0f;
    }
    if (ctx.gpr[2] == 0u) return 1.0f;
    const float fraction = std::bit_cast<float>(memory.load32(result + kFractionOffset));
    return std::isfinite(fraction) ? std::clamp(fraction, 0.0f, 1.0f) : 1.0f;
}

} // namespace sarusaru::game
