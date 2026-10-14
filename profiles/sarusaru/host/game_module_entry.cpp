// The game module's interface (game_module.hpp), compiled with the generated
// code into sarusaru_game: what it was built for, and its registration.
#include "game_abi.hpp"
#include "psprecomp/runtime.hpp"

#if defined(_WIN32)
#define SARUSARU_GAME_EXPORT extern "C" __declspec(dllexport)
#else
#define SARUSARU_GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

SARUSARU_GAME_EXPORT const char *sarusaru_game_abi() { return sarusaru::game_module_abi.data(); }

SARUSARU_GAME_EXPORT void sarusaru_register_game(psprecomp::Runtime &runtime) {
    psprecomp::register_generated_functions(runtime);
}
