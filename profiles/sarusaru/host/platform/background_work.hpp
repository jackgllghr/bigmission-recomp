#pragma once

// Long work that must carry on while the player is in another app (installing
// the game). On Android a backgrounded app may be stopped or killed, so the
// activity runs a foreground service with a progress notification while the
// work lasts (SaruSaruActivity, InstallService). Elsewhere nothing is needed.
//
// Callable from any thread; the calls for one piece of work must not overlap.
#include <optional>
#include <string_view>

namespace sarusaru::platform {

#if defined(__ANDROID__)
// Starts the work, or says how it is going: `fraction` in [0, 1], or none
// while the end is unknown.
void show_background_work(std::string_view title, std::string_view text, std::optional<float> fraction);
// The work ended. A non-empty `result` stays as a notification if the player
// is in another app.
void end_background_work(std::string_view title, std::string_view result);
#else
inline void show_background_work(std::string_view, std::string_view, std::optional<float>) {}
inline void end_background_work(std::string_view, std::string_view) {}
#endif

} // namespace sarusaru::platform
