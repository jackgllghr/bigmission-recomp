find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Every program of the profile goes to bin/, beside the shared runtime and
# the other libraries (on Windows the only place it looks for them).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

# FFmpeg (LGPL, dynamically linked) decodes the game's ATRAC music.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FFmpeg.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Graphics.cmake")
# The launcher's texts (Japanese included) are UTF-8 source.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# Where things live on the player's machine, and the files its picker gives.
add_library(sarusaru_platform STATIC host/platform/paths.cpp host/platform/documents.cpp)
target_include_directories(sarusaru_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
if(ANDROID)
    if(NOT SARUSARU_HAS_RENDERER)
        message(FATAL_ERROR "sarusaru: Android builds need the host shell (SARUSARU_RENDERER)")
    endif()
    # The app's storage is SDL's to report (SDL_GetAndroidInternalStoragePath).
    target_link_libraries(sarusaru_platform PUBLIC SDL3::SDL3)
endif()
# Recognising the player's disc image, for the launcher and the game session.
add_library(sarusaru_disc STATIC host/game_disc.cpp host/umd/disc_image.cpp)
target_include_directories(sarusaru_disc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_link_libraries(sarusaru_disc PUBLIC psprecomp_runtime sarusaru_platform)

if(SARUSARU_HAS_RENDERER)
    add_library(sarusaru_controls STATIC host/shell/settings.cpp host/shell/input.cpp)
    target_include_directories(sarusaru_controls PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_controls PUBLIC SDL3::SDL3 psprecomp_runtime)
    add_library(sarusaru_shell STATIC host/shell/vulkan_loader.cpp host/shell/vulkan_context.cpp)
    target_include_directories(sarusaru_shell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_shell PUBLIC SDL3::SDL3 Vulkan::Headers sarusaru_platform)
    add_executable(sarusaru_vulkan_probe host/shell/vulkan_probe.cpp)
    target_link_libraries(sarusaru_vulkan_probe PRIVATE sarusaru_shell)
    set_target_properties(sarusaru_vulkan_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")

    # The launcher and the port's screens: their logic (tested without a
    # window), then the screens themselves on Dear ImGui.
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Ui.cmake")
    add_library(sarusaru_ui_logic STATIC host/ui/settings_rows.cpp host/ui/text.cpp
        host/ui/bindings.cpp)
    target_include_directories(sarusaru_ui_logic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_ui_logic PUBLIC sarusaru_controls)
    add_library(sarusaru_ui STATIC host/ui/layer.cpp host/ui/ui_renderer.cpp host/ui/theme.cpp host/ui/widgets.cpp
        host/ui/launcher.cpp host/ui/ui_script.cpp)
    target_link_libraries(sarusaru_ui PUBLIC sarusaru_ui_logic sarusaru_disc sarusaru_shell sarusaru_imgui sarusaru_install)
    # Texture dump mode and on-device HD texture packs (PNG through SDL).
    add_library(sarusaru_textures STATIC host/textures/texture_dump.cpp host/textures/hd_pack.cpp
        host/textures/disc_textures.cpp host/textures/recipe.cpp host/textures/image.cpp)
    target_link_libraries(sarusaru_textures PUBLIC sarusaru_gpu sarusaru_disc SDL3::SDL3)
    add_library(sarusaru_install STATIC host/install/process.cpp host/install/download.cpp host/install/toolchain.cpp
        host/install/game_builder.cpp host/install/pack_download.cpp host/install/texture_build.cpp)
    target_link_libraries(sarusaru_install PUBLIC sarusaru_game_module sarusaru_disc sarusaru_textures SDL3::SDL3)
    add_dependencies(sarusaru_install psp_recomp)
endif()
# Shift-JIS text (guest output, later the translation tools), through iconv
# where the platform has it.
add_library(sarusaru_text STATIC host/text/shift_jis.cpp)
target_include_directories(sarusaru_text PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
find_package(Iconv)
if(Iconv_FOUND)
    target_link_libraries(sarusaru_text PRIVATE Iconv::Iconv)
    target_compile_definitions(sarusaru_text PRIVATE SARUSARU_HAS_ICONV=1)
endif()
add_library(sarusaru_atrac STATIC host/audio/atrac.cpp host/audio/atrac_decoder.cpp
    host/movie/psmf_demuxer.cpp host/movie/avc_decoder.cpp host/movie/psmf_player.cpp)
target_include_directories(sarusaru_atrac PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_link_libraries(sarusaru_atrac PUBLIC psprecomp_runtime)
if(SARUSARU_HAS_FFMPEG)
    target_compile_definitions(sarusaru_atrac PRIVATE SARUSARU_HAS_FFMPEG=1)
    target_link_libraries(sarusaru_atrac PUBLIC sarusaru_ffmpeg)
endif()

# GE display-list state and texture decoding (adapted from Yakumo).
add_library(sarusaru_frame_rate STATIC host/game/frame_rate.cpp host/game/gameplay_camera.cpp host/game/intro_logos.cpp host/game/orbit_camera.cpp)
target_include_directories(sarusaru_frame_rate PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_link_libraries(sarusaru_frame_rate PUBLIC psprecomp_runtime)

add_library(sarusaru_gpu STATIC host/gpu/ge_state.cpp host/gpu/texture_decode.cpp host/gpu/texture_hash.cpp host/gpu/frame_interpolator.cpp host/gpu/fov.cpp)
target_include_directories(sarusaru_gpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_link_libraries(sarusaru_gpu PUBLIC psprecomp_runtime psprecomp_core)

add_library(sarusaru_translation STATIC host/translation/texture_pack.cpp host/translation/glyph_runs.cpp host/translation/dialogue.cpp)
target_include_directories(sarusaru_translation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_link_libraries(sarusaru_translation PUBLIC sarusaru_gpu psprecomp_core)

# PSP-format save data. aes.c is tiny-AES-c, compiled as C++ like the rest.
add_library(sarusaru_savedata STATIC
    host/save_data/aes128.cpp host/save_data/param_sfo.cpp host/save_data/savedata_crypto.cpp
    host/save_data/savedata_store.cpp host/utility/savedata_dialog.cpp third_party/tiny_aes/aes.c)
set_source_files_properties(third_party/tiny_aes/aes.c PROPERTIES LANGUAGE CXX)
target_include_directories(sarusaru_savedata PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host" "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(sarusaru_savedata PUBLIC psprecomp_runtime sarusaru_platform)

if(PSPRECOMP_BUILD_PROFILE_TESTS)
    enable_testing()
    add_executable(sarusaru_translation_pack_tests tests/translation_pack_tests.cpp)
    target_link_libraries(sarusaru_translation_pack_tests PRIVATE sarusaru_translation)
    add_test(NAME sarusaru_translation_pack_tests COMMAND sarusaru_translation_pack_tests)
    add_executable(sarusaru_frame_rate_tests tests/frame_rate_tests.cpp
        host/display/display.cpp host/kernel/early_kernel.cpp)
    target_link_libraries(sarusaru_frame_rate_tests PRIVATE sarusaru_frame_rate)
    add_test(NAME sarusaru_frame_rate_tests COMMAND sarusaru_frame_rate_tests)
    if(SARUSARU_HAS_RENDERER)
        add_executable(sarusaru_vulkan_context_tests tests/vulkan_context_tests.cpp)
        target_link_libraries(sarusaru_vulkan_context_tests PRIVATE sarusaru_shell)
        add_test(NAME sarusaru_vulkan_context_tests COMMAND sarusaru_vulkan_context_tests)
        add_executable(sarusaru_settings_tests tests/settings_tests.cpp)
        target_link_libraries(sarusaru_settings_tests PRIVATE sarusaru_controls)
        set_target_properties(sarusaru_settings_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
        add_test(NAME sarusaru_settings_tests COMMAND sarusaru_settings_tests)
        add_executable(sarusaru_hd_texture_tests tests/hd_texture_tests.cpp)
        target_link_libraries(sarusaru_hd_texture_tests PRIVATE sarusaru_textures)
        set_target_properties(sarusaru_hd_texture_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
        add_test(NAME sarusaru_hd_texture_tests COMMAND sarusaru_hd_texture_tests)
        # Making a pack from an invented disc, with a nearest-neighbour stand-in for the upscaler.
        add_executable(sarusaru_fake_upscaler tests/fake_upscaler.cpp)
        target_link_libraries(sarusaru_fake_upscaler PRIVATE sarusaru_textures)
        set_target_properties(sarusaru_fake_upscaler PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
        add_executable(sarusaru_texture_recipe_tests tests/texture_recipe_tests.cpp)
        target_link_libraries(sarusaru_texture_recipe_tests PRIVATE sarusaru_install)
        target_compile_definitions(sarusaru_texture_recipe_tests PRIVATE
            SARUSARU_FAKE_UPSCALER="$<TARGET_FILE:sarusaru_fake_upscaler>")
        add_dependencies(sarusaru_texture_recipe_tests sarusaru_fake_upscaler)
        set_target_properties(sarusaru_texture_recipe_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
        add_test(NAME sarusaru_texture_recipe_tests COMMAND sarusaru_texture_recipe_tests)
        add_executable(sarusaru_launcher_tests tests/launcher_tests.cpp)
        target_include_directories(sarusaru_launcher_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        target_link_libraries(sarusaru_launcher_tests PRIVATE sarusaru_ui_logic sarusaru_disc)
        set_target_properties(sarusaru_launcher_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
        add_test(NAME sarusaru_launcher_tests COMMAND sarusaru_launcher_tests)
    endif()
    add_test(NAME sarusaru_prepare_tests
            COMMAND "${Python3_EXECUTABLE}" -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)
    add_executable(sarusaru_startup_hle_tests
        tests/startup_hle_tests.cpp host/startup_hle.cpp)
    target_include_directories(sarusaru_startup_hle_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_startup_hle_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_startup_hle_tests COMMAND sarusaru_startup_hle_tests)
    add_executable(sarusaru_early_kernel_tests
        tests/early_kernel_tests.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_early_kernel_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_early_kernel_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_early_kernel_tests COMMAND sarusaru_early_kernel_tests)
    add_executable(sarusaru_umd_tests
        tests/umd_tests.cpp host/umd/disc_image.cpp host/umd/umd_drive.cpp host/platform/documents.cpp
        host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_umd_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_umd_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_umd_tests COMMAND sarusaru_umd_tests)
    add_executable(sarusaru_intro_logos_tests tests/intro_logos_tests.cpp
        host/ctrl/controller.cpp host/kernel/early_kernel.cpp)
    target_link_libraries(sarusaru_intro_logos_tests PRIVATE sarusaru_frame_rate)
    add_test(NAME sarusaru_intro_logos_tests COMMAND sarusaru_intro_logos_tests)
    add_executable(sarusaru_ctrl_tests tests/ctrl_tests.cpp host/ctrl/controller.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_ctrl_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_ctrl_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_ctrl_tests COMMAND sarusaru_ctrl_tests)
    add_executable(sarusaru_display_tests tests/display_tests.cpp host/display/display.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_display_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_display_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_display_tests COMMAND sarusaru_display_tests)
    add_executable(sarusaru_sync_tests tests/sync_tests.cpp host/kernel/sync.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_sync_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_sync_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_sync_tests COMMAND sarusaru_sync_tests)
    add_executable(sarusaru_interrupt_tests tests/interrupt_tests.cpp host/kernel/interrupts.cpp
        host/kernel/sync.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_interrupt_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_interrupt_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_interrupt_tests COMMAND sarusaru_interrupt_tests)
    add_executable(sarusaru_ge_tests tests/ge_tests.cpp host/ge/ge.cpp host/kernel/early_kernel.cpp)
    target_link_libraries(sarusaru_ge_tests PRIVATE sarusaru_gpu)
    target_include_directories(sarusaru_ge_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_ge_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_ge_tests COMMAND sarusaru_ge_tests)
    add_executable(sarusaru_ge_state_tests tests/ge_state_tests.cpp)
    target_link_libraries(sarusaru_ge_state_tests PRIVATE sarusaru_gpu)
    add_test(NAME sarusaru_ge_state_tests COMMAND sarusaru_ge_state_tests)
    add_executable(sarusaru_frame_interpolator_tests tests/frame_interpolator_tests.cpp)
    target_link_libraries(sarusaru_frame_interpolator_tests PRIVATE sarusaru_gpu)
    add_test(NAME sarusaru_frame_interpolator_tests COMMAND sarusaru_frame_interpolator_tests)
    add_executable(sarusaru_fov_tests tests/fov_tests.cpp)
    target_link_libraries(sarusaru_fov_tests PRIVATE sarusaru_gpu)
    add_test(NAME sarusaru_fov_tests COMMAND sarusaru_fov_tests)
    add_executable(sarusaru_orbit_camera_tests tests/orbit_camera_tests.cpp)
    target_link_libraries(sarusaru_orbit_camera_tests PRIVATE sarusaru_frame_rate)
    add_test(NAME sarusaru_orbit_camera_tests COMMAND sarusaru_orbit_camera_tests)
    add_executable(sarusaru_gameplay_camera_tests tests/gameplay_camera_tests.cpp host/display/display.cpp
        host/kernel/early_kernel.cpp)
    target_link_libraries(sarusaru_gameplay_camera_tests PRIVATE sarusaru_frame_rate)
    add_test(NAME sarusaru_gameplay_camera_tests COMMAND sarusaru_gameplay_camera_tests)
    add_executable(sarusaru_texture_decode_tests tests/texture_decode_tests.cpp)
    target_link_libraries(sarusaru_texture_decode_tests PRIVATE sarusaru_gpu)
    add_test(NAME sarusaru_texture_decode_tests COMMAND sarusaru_texture_decode_tests)
    add_executable(sarusaru_io_tests tests/io_tests.cpp host/io/io_manager.cpp host/umd/disc_image.cpp)
    target_link_libraries(sarusaru_io_tests PRIVATE sarusaru_text)
    add_executable(sarusaru_shift_jis_tests tests/shift_jis_tests.cpp)
    target_link_libraries(sarusaru_shift_jis_tests PRIVATE sarusaru_text)
    if(Iconv_FOUND)
        target_compile_definitions(sarusaru_shift_jis_tests PRIVATE SARUSARU_HAS_ICONV=1)
    endif()
    add_test(NAME sarusaru_shift_jis_tests COMMAND sarusaru_shift_jis_tests)
    target_include_directories(sarusaru_io_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_io_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_io_tests COMMAND sarusaru_io_tests)
    add_executable(sarusaru_rtc_tests tests/rtc_tests.cpp host/rtc/rtc.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_rtc_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_rtc_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_rtc_tests COMMAND sarusaru_rtc_tests)
    add_executable(sarusaru_module_tests tests/module_tests.cpp host/modules/utility_modules.cpp)
    target_include_directories(sarusaru_module_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_module_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_module_tests COMMAND sarusaru_module_tests)
    add_executable(sarusaru_callback_tests tests/callback_tests.cpp host/kernel/early_kernel.cpp
        host/umd/disc_image.cpp host/umd/umd_drive.cpp)
    target_include_directories(sarusaru_callback_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_callback_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_callback_tests COMMAND sarusaru_callback_tests)
    add_executable(sarusaru_audio_tests tests/audio_tests.cpp host/audio/audio.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_audio_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_audio_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_audio_tests COMMAND sarusaru_audio_tests)
    add_executable(sarusaru_sas_tests tests/sas_tests.cpp host/sas/sas.cpp)
    target_include_directories(sarusaru_sas_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_sas_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_sas_tests COMMAND sarusaru_sas_tests)
    add_executable(sarusaru_savedata_tests tests/savedata_tests.cpp)
    target_link_libraries(sarusaru_savedata_tests PRIVATE sarusaru_savedata)
    add_test(NAME sarusaru_savedata_tests COMMAND sarusaru_savedata_tests)
    add_executable(sarusaru_savedata_dialog_tests tests/savedata_dialog_tests.cpp)
    target_link_libraries(sarusaru_savedata_dialog_tests PRIVATE sarusaru_savedata)
    add_test(NAME sarusaru_savedata_dialog_tests COMMAND sarusaru_savedata_dialog_tests)
    add_executable(sarusaru_atrac_tests tests/atrac_tests.cpp)
    target_link_libraries(sarusaru_atrac_tests PRIVATE sarusaru_atrac)
    sarusaru_use_ffmpeg(sarusaru_atrac_tests)
    add_test(NAME sarusaru_atrac_tests COMMAND sarusaru_atrac_tests)
    add_executable(sarusaru_psmf_player_tests tests/psmf_player_tests.cpp host/kernel/early_kernel.cpp)
    target_link_libraries(sarusaru_psmf_player_tests PRIVATE sarusaru_atrac)
    sarusaru_use_ffmpeg(sarusaru_psmf_player_tests)
    add_test(NAME sarusaru_psmf_player_tests COMMAND sarusaru_psmf_player_tests)
    add_executable(sarusaru_module_manager_tests tests/module_manager_tests.cpp host/modules/module_manager.cpp
        host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_module_manager_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_module_manager_tests PRIVATE psprecomp_runtime)
    add_test(NAME sarusaru_module_manager_tests COMMAND sarusaru_module_manager_tests)
endif()

# The supported game, and the runtime interface its module is built for.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/supported_game.json" supported_game)
string(JSON SARUSARU_EXECUTABLE_SHA256 GET "${supported_game}" executable_sha256)
string(JSON SARUSARU_EXECUTABLE_SIZE GET "${supported_game}" executable_size)
string(JSON SARUSARU_LOAD_BASE GET "${supported_game}" load_base)
string(JSON SARUSARU_UNIT_SPAN GET "${supported_game}" unit_span)
string(JSON SARUSARU_DISC_ID GET "${supported_game}" disc_id)
configure_file(host/game_identity.hpp.in game_identity.hpp @ONLY)
# A module built from other headers, another code generator or another game
# must be rebuilt, so all of them are in the hash (host/game_module.hpp).
file(GLOB _sarusaru_abi_inputs CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/psprecomp/*.hpp")
list(APPEND _sarusaru_abi_inputs "${PROJECT_SOURCE_DIR}/tools/codegen_main.cpp" "${PROJECT_SOURCE_DIR}/src/decoder.cpp"
    "${PROJECT_SOURCE_DIR}/src/program_analysis.cpp" "${PROJECT_SOURCE_DIR}/src/elf32.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/config/supported_game.json" "${CMAKE_CURRENT_SOURCE_DIR}/host/game_module_entry.cpp")
list(SORT _sarusaru_abi_inputs)
set(_sarusaru_abi "${PROJECT_VERSION} ${PSPRECOMP_AOT_ASSUME_NO_WRITE_WATCH} ${PSPRECOMP_AOT_PRODUCTION_FASTPATHS}")
foreach(input IN LISTS _sarusaru_abi_inputs)
    # Line endings depend on the checkout, not on the interface.
    file(READ "${input}" _sarusaru_abi_text)
    string(REPLACE "\r\n" "\n" _sarusaru_abi_text "${_sarusaru_abi_text}")
    string(SHA256 _sarusaru_abi_hash "${_sarusaru_abi_text}")
    string(APPEND _sarusaru_abi " ${_sarusaru_abi_hash}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_sarusaru_abi_inputs})
string(SHA256 SARUSARU_GAME_ABI "${_sarusaru_abi}")
string(SUBSTRING "${SARUSARU_GAME_ABI}" 0 16 SARUSARU_GAME_ABI)
configure_file(host/game_abi.hpp.in game_abi.hpp @ONLY)

# Generated code is optimised: at -O0 each unit's stack frame is so large
# that chained guest calls (up to 48 deep) overflow the native stack.
# SARUSARU_AOT_OPTIMIZATION trades build time for speed (-O0 for quick
# diagnostic rebuilds, run with SARUSARU_CHAINING unset).
set(SARUSARU_AOT_OPTIMIZATION "-O1" CACHE STRING "Optimisation flag for the generated game code")

# Loading the game module, and (with the launcher) building it on the
# player's machine with the compiler that matches this build.
add_library(sarusaru_game_module STATIC host/game_module.cpp)
target_include_directories(sarusaru_game_module PUBLIC "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_link_libraries(sarusaru_game_module PUBLIC psprecomp_runtime sarusaru_platform ${CMAKE_DL_LIBS})
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LlvmMingw.cmake")
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SARUSARU_INSTALL_TOOLCHAIN apple)
elseif(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(SARUSARU_INSTALL_TOOLCHAIN llvm-mingw)
elseif(UNIX AND NOT APPLE AND NOT ANDROID AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(SARUSARU_INSTALL_TOOLCHAIN gnu)
else()
    # MSVC and the rest: the game module comes from SARUSARU_GAME_MODULE only.
    set(SARUSARU_INSTALL_TOOLCHAIN "")
endif()
set(SARUSARU_MODULE_DEFINES PSPRECOMP_SHARED_RUNTIME)
foreach(option PSPRECOMP_AOT_ASSUME_NO_WRITE_WATCH PSPRECOMP_AOT_PRODUCTION_FASTPATHS)
    if(${option})
        string(APPEND SARUSARU_MODULE_DEFINES " ${option}=1")
    endif()
endforeach()
configure_file(host/install/game_build_config.hpp.in install/game_build_config.hpp @ONLY)
# The optional English translation pack a player can download from the
# launcher. It is published apart from the port (sarusaru-translations), so a
# build offers it only when given its URL, SHA-256 and size.
set(SARUSARU_ENGLISH_PACK_URL "" CACHE STRING "URL of the English translation pack the launcher offers (empty: none)")
set(SARUSARU_ENGLISH_PACK_SHA256 "" CACHE STRING "SHA-256 of the English translation pack")
set(SARUSARU_ENGLISH_PACK_SIZE "0" CACHE STRING "Size in bytes of the English translation pack")
if(NOT SARUSARU_ENGLISH_PACK_URL STREQUAL "")
    if(NOT SARUSARU_ENGLISH_PACK_URL MATCHES "^https://" OR NOT SARUSARU_ENGLISH_PACK_SHA256 MATCHES "^[0-9a-f]+$"
       OR NOT SARUSARU_ENGLISH_PACK_SIZE MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "SARUSARU_ENGLISH_PACK_URL needs an https URL, a lowercase SHA-256 and a size in bytes")
    endif()
    string(LENGTH "${SARUSARU_ENGLISH_PACK_SHA256}" _sarusaru_pack_hash_length)
    if(NOT _sarusaru_pack_hash_length EQUAL 64)
        message(FATAL_ERROR "SARUSARU_ENGLISH_PACK_SHA256 must be 64 hexadecimal digits")
    endif()
endif()
configure_file(host/install/pack_config.hpp.in install/pack_config.hpp @ONLY)
# The optional HD texture recipe: which textures to upscale and how, never
# pixels. The launcher downloads it and upscales the player's own textures on
# their device (docs/hd-textures.md). file:// is for trying a local recipe.
set(SARUSARU_TEXTURE_RECIPE_URL "" CACHE STRING "URL of the HD texture recipe the launcher offers (empty: none)")
set(SARUSARU_TEXTURE_RECIPE_SHA256 "" CACHE STRING "SHA-256 of the HD texture recipe")
set(SARUSARU_TEXTURE_RECIPE_SIZE "0" CACHE STRING "Size in bytes of the HD texture recipe")
if(NOT SARUSARU_TEXTURE_RECIPE_URL STREQUAL "")
    string(LENGTH "${SARUSARU_TEXTURE_RECIPE_SHA256}" _sarusaru_recipe_hash_length)
    if(NOT SARUSARU_TEXTURE_RECIPE_URL MATCHES "^(https|file)://" OR NOT SARUSARU_TEXTURE_RECIPE_SHA256 MATCHES "^[0-9a-f]+$"
       OR NOT _sarusaru_recipe_hash_length EQUAL 64 OR NOT SARUSARU_TEXTURE_RECIPE_SIZE MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "SARUSARU_TEXTURE_RECIPE_URL needs an https (or file) URL, a 64-digit lowercase SHA-256 and a size in bytes")
    endif()
    if(SARUSARU_TEXTURE_RECIPE_URL MATCHES "^file://")
        message(WARNING "SARUSARU_TEXTURE_RECIPE_URL is a local file; release builds need an https URL")
    endif()
endif()
configure_file(host/install/texture_config.hpp.in install/texture_config.hpp @ONLY)
# llvm-mingw links its C++ runtime as DLLs; they go beside the executables,
# and the module a player builds uses the same ones.
if(MINGW)
    get_filename_component(_sarusaru_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(dll libc++.dll libunwind.dll)
        find_file(_sarusaru_mingw_dll_${dll} "${dll}" NO_DEFAULT_PATH
            PATHS "${_sarusaru_mingw_bin}/../x86_64-w64-mingw32/bin" "${_sarusaru_mingw_bin}")
        if(_sarusaru_mingw_dll_${dll})
            configure_file("${_sarusaru_mingw_dll_${dll}}" "${CMAKE_BINARY_DIR}/bin/${dll}" COPYONLY)
        endif()
    endforeach()
    # They are shipped unmodified, so their licence (Apache-2.0 with LLVM exceptions) goes with them.
    if(EXISTS "${_sarusaru_mingw_bin}/../LICENSE.TXT")
        configure_file("${_sarusaru_mingw_bin}/../LICENSE.TXT" "${CMAKE_BINARY_DIR}/bin/llvm-mingw-LICENSE.txt" COPYONLY)
    endif()
endif()

# What a launcher needs beside it to build the module: the code generator,
# the runtime's headers and (for Windows) its import library, and the
# module's interface. Release packages carry the same files.
# The framework's programs join them in bin/ (on Windows they need the C++ runtime DLLs there).
foreach(program psp_recomp psp_analyze dump_function psprecomp_tests)
    if(TARGET ${program})
        set_target_properties(${program} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
endforeach()
set(_sarusaru_sdk "${CMAKE_BINARY_DIR}/bin/sdk")
file(REMOVE_RECURSE "${_sarusaru_sdk}/include")
file(COPY "${PROJECT_SOURCE_DIR}/include/psprecomp" DESTINATION "${_sarusaru_sdk}/include")
configure_file("${CMAKE_CURRENT_BINARY_DIR}/game_abi.hpp" "${_sarusaru_sdk}/include/game_abi.hpp" COPYONLY)
configure_file(host/game_module_entry.cpp "${_sarusaru_sdk}/game_module_entry.cpp" COPYONLY)
if(WIN32)
    add_custom_command(OUTPUT "${_sarusaru_sdk}/lib/libpsprecomp_runtime.dll.a"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_sarusaru_sdk}/lib"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_LINKER_FILE:psprecomp_runtime>"
            "${_sarusaru_sdk}/lib/libpsprecomp_runtime.dll.a"
        DEPENDS psprecomp_runtime)
    add_custom_target(sarusaru_sdk_import_library ALL DEPENDS "${_sarusaru_sdk}/lib/libpsprecomp_runtime.dll.a")
endif()
# The game runs on the main thread with deeply chained guest calls;
# Windows reserves only 1 MiB for it by default (macOS and Linux 8 MiB).
function(sarusaru_game_stack target)
    if(MSVC)
        target_link_options(${target} PRIVATE /STACK:67108864)
    elseif(WIN32)
        target_link_options(${target} PRIVATE -Wl,--stack,67108864)
    endif()
endfunction()

# Everything one running game needs, shared by the bootstrap and the shell.
add_library(sarusaru_session STATIC host/session.cpp host/startup_hle.cpp host/kernel/early_kernel.cpp
    host/kernel/sync.cpp host/kernel/interrupts.cpp host/umd/umd_drive.cpp host/ctrl/controller.cpp
    host/display/display.cpp host/ge/ge.cpp host/io/io_manager.cpp
    host/rtc/rtc.cpp host/modules/utility_modules.cpp host/modules/module_manager.cpp host/audio/audio.cpp
    host/sas/sas.cpp)
target_include_directories(sarusaru_session PUBLIC "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_link_libraries(sarusaru_session PUBLIC sarusaru_game_module sarusaru_disc sarusaru_savedata sarusaru_atrac sarusaru_gpu
    sarusaru_text sarusaru_frame_rate)
if(SARUSARU_HAS_FFMPEG)
    target_compile_definitions(sarusaru_session PRIVATE SARUSARU_HAS_FFMPEG=1)
endif()
add_executable(sarusaru_bootstrap host/bootstrap.cpp)
target_link_libraries(sarusaru_bootstrap PRIVATE sarusaru_session)
sarusaru_use_ffmpeg(sarusaru_bootstrap)
sarusaru_game_stack(sarusaru_bootstrap)
set_target_properties(sarusaru_bootstrap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(SARUSARU_HAS_RENDERER)
    # The windowed game. The GE shaders are compiled to SPIR-V at build
    # time and embedded, so the executable needs no shader files.
    set(_sarusaru_shaders "${CMAKE_CURRENT_BINARY_DIR}/ge_shaders.inc")
    add_custom_command(OUTPUT "${_sarusaru_shaders}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.py"
            "$<TARGET_FILE:${SARUSARU_GLSLANG}>" "${_sarusaru_shaders}"
            "kGeVertexShader=${CMAKE_CURRENT_SOURCE_DIR}/host/gpu/shaders/ge.vert"
            "kGeFragmentShader=${CMAKE_CURRENT_SOURCE_DIR}/host/gpu/shaders/ge.frag"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.py" "${CMAKE_CURRENT_SOURCE_DIR}/host/gpu/shaders/ge.vert"
            "${CMAKE_CURRENT_SOURCE_DIR}/host/gpu/shaders/ge.frag" ${SARUSARU_GLSLANG}
        COMMENT "Compiling the GE shaders")
    # The logo is embedded too, as the window (and so task bar and Dock)
    # icon; on Windows it is also the executable's file icon.
    set(_sarusaru_logo "${PROJECT_SOURCE_DIR}/include/image/logo.png")
    set(_sarusaru_icon "${CMAKE_CURRENT_BINARY_DIR}/window_icon.inc")
    add_custom_command(OUTPUT "${_sarusaru_icon}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_file.py"
            "${_sarusaru_icon}" "kWindowIconPng=${_sarusaru_logo}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_file.py" "${_sarusaru_logo}"
        COMMENT "Embedding the window icon")
    set(_sarusaru_resources "")
    if(WIN32)
        enable_language(RC)
        list(APPEND _sarusaru_resources resources/sarusaru.rc)
        set_source_files_properties(resources/sarusaru.rc PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/sarusaru.ico")
    endif()
    set(_sarusaru_shell_sources host/shell/main.cpp host/shell/ge_renderer.cpp
        host/shell/audio_output.cpp "${_sarusaru_shaders}" "${_sarusaru_icon}"
        ${_sarusaru_resources})
    if(ANDROID)
        # On Android the shell is libmain.so, whose SDL_main SDLActivity calls
        # (packaging/android).
        add_library(sarusaru SHARED ${_sarusaru_shell_sources})
        set_target_properties(sarusaru PROPERTIES OUTPUT_NAME main LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/lib")
        target_link_libraries(sarusaru PRIVATE log)
        # The shipped resources (fonts, licences, the title) become the APK's
        # assets; the Gradle build names the directory for each build type.
        set(SARUSARU_ANDROID_ASSETS "" CACHE PATH "Where to put the APK's assets (set by the Gradle build)")
        if(NOT SARUSARU_ANDROID_ASSETS STREQUAL "")
            # FFmpeg's notices sit beside its libraries elsewhere; an APK's lib/ holds only libraries.
            set(_sarusaru_android_notices "")
            if(SARUSARU_HAS_FFMPEG)
                list(APPEND _sarusaru_android_notices COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${CMAKE_BINARY_DIR}/bin/lib/FFmpeg-COPYING.LGPLv2.1.txt" "${CMAKE_BINARY_DIR}/bin/lib/FFmpeg-SOURCE.txt"
                    "${SARUSARU_ANDROID_ASSETS}/resources/")
            endif()
            add_custom_target(sarusaru_android_assets
                COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${CMAKE_BINARY_DIR}/bin/resources"
                    "${SARUSARU_ANDROID_ASSETS}/resources"
                ${_sarusaru_android_notices}
                COMMENT "Copying the shipped resources to the APK's assets")
            add_dependencies(sarusaru sarusaru_android_assets)
        endif()
    else()
        add_executable(sarusaru ${_sarusaru_shell_sources})
    endif()
    if(WIN32)
        target_include_directories(sarusaru PRIVATE
            "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}/resources>")
    endif()
    target_link_libraries(sarusaru PRIVATE sarusaru_session sarusaru_ui sarusaru_translation)
    sarusaru_use_ffmpeg(sarusaru)
    sarusaru_game_stack(sarusaru)
    # Shipped: only the relative rpath, never the build tree's (which CMake
    # adds for the imported FFmpeg libraries and would name this machine).
    # In the macOS app bundle (scripts/package.py) the libraries are in Contents/Frameworks.
    if(APPLE)
        set_target_properties(sarusaru PROPERTIES BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "@loader_path/lib;@executable_path/../Frameworks")
    elseif(UNIX)
        set_target_properties(sarusaru PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN/lib")
    endif()
    set_target_properties(sarusaru PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
    if(PSPRECOMP_BUILD_PROFILE_TESTS AND NOT ANDROID)
        add_test(NAME sarusaru_shell_help COMMAND sarusaru --help)
    endif()
endif()
if(PSPRECOMP_BUILD_PROFILE_TESTS)
    add_test(NAME sarusaru_bootstrap_help COMMAND sarusaru_bootstrap --help)
    add_test(NAME sarusaru_bootstrap_missing_input COMMAND sarusaru_bootstrap)
    set_tests_properties(sarusaru_bootstrap_missing_input PROPERTIES WILL_FAIL TRUE)
endif()

# The game module from the locally generated corpus, beside the executables,
# for development. Players' launchers build theirs (host/install/).
option(SARUSARU_GAME_MODULE "Build the game module from the locally generated code in generated/" OFF)
if(SARUSARU_GAME_MODULE)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/generated_registry.cpp")
        message(FATAL_ERROR "Generate the SaruSaru AOT corpus first; see profiles/sarusaru/README.md")
    endif()
    file(GLOB aot_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated/generated_*.cpp")
    add_library(sarusaru_game SHARED ${aot_sources} host/game_module_entry.cpp)
    target_include_directories(sarusaru_game PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(sarusaru_game PRIVATE psprecomp_runtime)
    if(MSVC)
        target_compile_options(sarusaru_game PRIVATE /bigobj)
    else()
        target_compile_options(sarusaru_game PRIVATE ${SARUSARU_AOT_OPTIMIZATION})
    endif()
    set_target_properties(sarusaru_game PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
    if(APPLE)
        set_target_properties(sarusaru_game PROPERTIES SUFFIX ".dylib")
    endif()
    add_dependencies(sarusaru_bootstrap sarusaru_game)
    if(TARGET sarusaru)
        add_dependencies(sarusaru sarusaru_game)
    endif()
endif()
