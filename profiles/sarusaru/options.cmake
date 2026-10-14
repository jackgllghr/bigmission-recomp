# Read by the root CMakeLists.txt before its options: the game's generated code
# is a module loaded at run time (host/game_module.hpp), sharing the host's runtime.
set(PSPRECOMP_SHARED_RUNTIME ON CACHE BOOL "")
