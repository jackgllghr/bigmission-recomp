#pragma once

// First-fit ranges within one fixed-size block, for suballocating image
// memory: freed ranges merge with their neighbours. Not thread-safe.
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>

namespace sarusaru::shell {

class RangeAllocator {
public:
    explicit RangeAllocator(std::uint64_t size) : size_(size) { free_.emplace(0u, size); }

    // The offset of `size` bytes aligned to `alignment` (a power of two), or
    // nothing when no free range fits.
    std::optional<std::uint64_t> allocate(std::uint64_t size, std::uint64_t alignment) {
        if (size == 0u) size = 1u;
        for (auto it = free_.begin(); it != free_.end(); ++it) {
            const std::uint64_t start = it->first, length = it->second;
            const std::uint64_t aligned = (start + alignment - 1u) & ~(alignment - 1u);
            if (aligned + size > start + length) continue;
            free_.erase(it);
            if (aligned > start) free_.emplace(start, aligned - start);
            if (aligned + size < start + length) free_.emplace(aligned + size, start + length - aligned - size);
            used_ += size;
            sizes_.emplace(aligned, size);
            return aligned;
        }
        return std::nullopt;
    }

    // Returns a range allocate() gave.
    void free(std::uint64_t offset) {
        const auto found = sizes_.find(offset);
        if (found == sizes_.end()) return;
        std::uint64_t start = offset, length = found->second;
        used_ -= length;
        sizes_.erase(found);
        auto next = free_.lower_bound(start);
        if (next != free_.end() && next->first == start + length) {
            length += next->second;
            next = free_.erase(next);
        }
        if (next != free_.begin()) {
            const auto previous = std::prev(next);
            if (previous->first + previous->second == start) {
                start = previous->first;
                length += previous->second;
                free_.erase(previous);
            }
        }
        free_.emplace(start, length);
    }

    [[nodiscard]] bool empty() const noexcept { return sizes_.empty(); }
    [[nodiscard]] std::uint64_t used() const noexcept { return used_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t free_ranges() const noexcept { return free_.size(); }

private:
    std::uint64_t size_;
    std::uint64_t used_{};
    std::map<std::uint64_t, std::uint64_t> free_;   // offset -> length
    std::map<std::uint64_t, std::uint64_t> sizes_;  // allocated offset -> length
};

} // namespace sarusaru::shell
