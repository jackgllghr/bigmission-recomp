#pragma once
#include "save_state/session_access.hpp"

// Adapted from Yakumo (https://github.com/TeamGDB/Yakumo, revision 39ac05f),
// MIT License; see profiles/sarusaru/THIRD_PARTY.md.

// Life cycle shared by the sceUtility dialogs.
//
//   InitStart -> INIT -> VISIBLE -> QUIT -> ShutdownStart -> FINISHED -> NONE
//
// The guest polls GetStatus (and, for some dialogs, Update) until the dialog
// reports QUIT, then calls ShutdownStart and waits for NONE. The host does a
// request's work in InitStart and each poll moves the status one step on, so
// every path, errors included, reaches QUIT within a few polls.
#include <cstdint>

namespace sarusaru::utility {

namespace dialog_status {
inline constexpr std::uint32_t kNone = 0u;
inline constexpr std::uint32_t kInit = 1u;
inline constexpr std::uint32_t kVisible = 2u;
inline constexpr std::uint32_t kQuit = 3u;
inline constexpr std::uint32_t kFinished = 4u;
} // namespace dialog_status

// Common header at the start of every dialog's parameter block.
namespace dialog_common {
inline constexpr std::uint32_t kSizeOffset = 0x00u;
inline constexpr std::uint32_t kResultOffset = 0x1Cu;
} // namespace dialog_common

// ShutdownStart when no dialog of that kind is active.
inline constexpr std::uint32_t kErrorInvalidStatus = 0x80110001u;

class DialogLifecycle {
    friend struct sarusaru::save_state::SessionCodec;
public:
    [[nodiscard]] bool active() const noexcept { return status_ != dialog_status::kNone; }
    [[nodiscard]] std::uint32_t status() const noexcept { return status_; }

    void start() noexcept { status_ = dialog_status::kInit; }

    // Returns the status to report for this poll, then advances it.
    std::uint32_t poll() noexcept {
        const std::uint32_t reported = status_;
        if (status_ == dialog_status::kInit)
            status_ = dialog_status::kVisible;
        else if (status_ == dialog_status::kVisible)
            status_ = dialog_status::kQuit;
        else if (status_ == dialog_status::kFinished)
            status_ = dialog_status::kNone;
        return reported;
    }

    // ShutdownStart. Returns false when the dialog had not quit yet; it is
    // shut down anyway, since its work is already done.
    bool shutdown() noexcept {
        const bool quit = status_ == dialog_status::kQuit;
        if (status_ != dialog_status::kNone) status_ = dialog_status::kFinished;
        return quit;
    }

private:
    std::uint32_t status_{dialog_status::kNone};
};

} // namespace sarusaru::utility
