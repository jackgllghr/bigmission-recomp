find_package(Python3 REQUIRED COMPONENTS Interpreter)

# FFmpeg (LGPL, dynamically linked) decodes the game's ATRAC music.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FFmpeg.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Graphics.cmake")
if(SARUSARU_HAS_RENDERER)
    add_library(sarusaru_controls STATIC host/shell/settings.cpp host/shell/input.cpp)
    target_include_directories(sarusaru_controls PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_controls PUBLIC SDL3::SDL3 psprecomp_core)
    add_library(sarusaru_shell STATIC host/shell/vulkan_loader.cpp)
    target_include_directories(sarusaru_shell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_shell PUBLIC SDL3::SDL3 Vulkan::Headers)
    add_executable(sarusaru_vulkan_probe host/shell/vulkan_probe.cpp)
    target_link_libraries(sarusaru_vulkan_probe PRIVATE sarusaru_shell)
    set_target_properties(sarusaru_vulkan_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
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
target_link_libraries(sarusaru_atrac PUBLIC psprecomp_core)
if(SARUSARU_HAS_FFMPEG)
    target_compile_definitions(sarusaru_atrac PRIVATE SARUSARU_HAS_FFMPEG=1)
    target_link_libraries(sarusaru_atrac PUBLIC sarusaru_ffmpeg)
endif()

# GE display-list state and texture decoding (adapted from Yakumo).
add_library(sarusaru_gpu STATIC host/gpu/ge_state.cpp host/gpu/texture_decode.cpp)
target_include_directories(sarusaru_gpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_link_libraries(sarusaru_gpu PUBLIC psprecomp_core)

# PSP-format save data. aes.c is tiny-AES-c, compiled as C++ like the rest.
add_library(sarusaru_savedata STATIC
    host/save_data/aes128.cpp host/save_data/param_sfo.cpp host/save_data/savedata_crypto.cpp
    host/save_data/savedata_store.cpp host/utility/savedata_dialog.cpp third_party/tiny_aes/aes.c)
set_source_files_properties(third_party/tiny_aes/aes.c PROPERTIES LANGUAGE CXX)
target_include_directories(sarusaru_savedata PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host" "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(sarusaru_savedata PUBLIC psprecomp_core)

if(PSPRECOMP_BUILD_PROFILE_TESTS)
    enable_testing()
    if(SARUSARU_HAS_RENDERER)
        add_executable(sarusaru_settings_tests tests/settings_tests.cpp)
        target_link_libraries(sarusaru_settings_tests PRIVATE sarusaru_controls)
        set_target_properties(sarusaru_settings_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
        add_test(NAME sarusaru_settings_tests COMMAND sarusaru_settings_tests)
    endif()
    add_test(NAME sarusaru_prepare_tests
            COMMAND "${Python3_EXECUTABLE}" -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)
    add_executable(sarusaru_startup_hle_tests
        tests/startup_hle_tests.cpp host/startup_hle.cpp)
    target_include_directories(sarusaru_startup_hle_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_startup_hle_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_startup_hle_tests COMMAND sarusaru_startup_hle_tests)
    add_executable(sarusaru_early_kernel_tests
        tests/early_kernel_tests.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_early_kernel_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_early_kernel_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_early_kernel_tests COMMAND sarusaru_early_kernel_tests)
    add_executable(sarusaru_umd_tests
        tests/umd_tests.cpp host/umd/disc_image.cpp host/umd/umd_drive.cpp
        host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_umd_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_umd_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_umd_tests COMMAND sarusaru_umd_tests)
    add_executable(sarusaru_ctrl_tests tests/ctrl_tests.cpp host/ctrl/controller.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_ctrl_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_ctrl_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_ctrl_tests COMMAND sarusaru_ctrl_tests)
    add_executable(sarusaru_display_tests tests/display_tests.cpp host/display/display.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_display_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_display_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_display_tests COMMAND sarusaru_display_tests)
    add_executable(sarusaru_sync_tests tests/sync_tests.cpp host/kernel/sync.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_sync_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_sync_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_sync_tests COMMAND sarusaru_sync_tests)
    add_executable(sarusaru_interrupt_tests tests/interrupt_tests.cpp host/kernel/interrupts.cpp
        host/kernel/sync.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_interrupt_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_interrupt_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_interrupt_tests COMMAND sarusaru_interrupt_tests)
    add_executable(sarusaru_ge_tests tests/ge_tests.cpp host/ge/ge.cpp host/kernel/early_kernel.cpp)
    target_link_libraries(sarusaru_ge_tests PRIVATE sarusaru_gpu)
    target_include_directories(sarusaru_ge_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_ge_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_ge_tests COMMAND sarusaru_ge_tests)
    add_executable(sarusaru_ge_state_tests tests/ge_state_tests.cpp)
    target_link_libraries(sarusaru_ge_state_tests PRIVATE sarusaru_gpu)
    add_test(NAME sarusaru_ge_state_tests COMMAND sarusaru_ge_state_tests)
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
    target_link_libraries(sarusaru_io_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_io_tests COMMAND sarusaru_io_tests)
    add_executable(sarusaru_rtc_tests tests/rtc_tests.cpp host/rtc/rtc.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_rtc_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_rtc_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_rtc_tests COMMAND sarusaru_rtc_tests)
    add_executable(sarusaru_module_tests tests/module_tests.cpp host/modules/utility_modules.cpp)
    target_include_directories(sarusaru_module_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_module_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_module_tests COMMAND sarusaru_module_tests)
    add_executable(sarusaru_callback_tests tests/callback_tests.cpp host/kernel/early_kernel.cpp
        host/umd/disc_image.cpp host/umd/umd_drive.cpp)
    target_include_directories(sarusaru_callback_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_callback_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_callback_tests COMMAND sarusaru_callback_tests)
    add_executable(sarusaru_audio_tests tests/audio_tests.cpp host/audio/audio.cpp host/kernel/early_kernel.cpp)
    target_include_directories(sarusaru_audio_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_audio_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_audio_tests COMMAND sarusaru_audio_tests)
    add_executable(sarusaru_sas_tests tests/sas_tests.cpp host/sas/sas.cpp)
    target_include_directories(sarusaru_sas_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_sas_tests PRIVATE psprecomp_core)
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
    target_link_libraries(sarusaru_module_manager_tests PRIVATE psprecomp_core)
    add_test(NAME sarusaru_module_manager_tests COMMAND sarusaru_module_manager_tests)
endif()

option(SARUSARU_BUILD_BOOTSTRAP "Build the diagnostic host with locally generated AOT code" OFF)
if(SARUSARU_BUILD_BOOTSTRAP)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/generated_registry.cpp")
        message(FATAL_ERROR "Generate the SaruSaru AOT corpus first; see profiles/sarusaru/README.md")
    endif()
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/supported_game.json" supported_game)
    string(JSON SARUSARU_EXECUTABLE_SHA256 GET "${supported_game}" executable_sha256)
    string(JSON SARUSARU_EXECUTABLE_SIZE GET "${supported_game}" executable_size)
    string(JSON SARUSARU_LOAD_BASE GET "${supported_game}" load_base)
    string(JSON SARUSARU_DISC_ID GET "${supported_game}" disc_id)
    configure_file(host/game_identity.hpp.in game_identity.hpp @ONLY)

    file(GLOB aot_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated/generated_*.cpp")
    add_library(sarusaru_aot STATIC ${aot_sources})
    target_link_libraries(sarusaru_aot PUBLIC psprecomp_core)
    # Generated code is optimised: at -O0 each unit's stack frame is so large
    # that chained guest calls (up to 48 deep) overflow the native stack.
    # SARUSARU_AOT_OPTIMIZATION trades build time for speed (-O0 for quick
    # diagnostic rebuilds, run with SARUSARU_CHAINING unset).
    set(SARUSARU_AOT_OPTIMIZATION "-O1" CACHE STRING "Optimisation flag for the generated game code")
    if(NOT MSVC)
        target_compile_options(sarusaru_aot PRIVATE ${SARUSARU_AOT_OPTIMIZATION})
    endif()

    # Everything one running game needs, shared by the bootstrap and the shell.
    add_library(sarusaru_session STATIC host/session.cpp host/startup_hle.cpp host/kernel/early_kernel.cpp
        host/kernel/sync.cpp host/kernel/interrupts.cpp host/umd/disc_image.cpp host/umd/umd_drive.cpp host/ctrl/controller.cpp
        host/display/display.cpp host/ge/ge.cpp host/io/io_manager.cpp
        host/rtc/rtc.cpp host/modules/utility_modules.cpp host/modules/module_manager.cpp host/audio/audio.cpp
        host/sas/sas.cpp)
    target_include_directories(sarusaru_session PUBLIC "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_session PUBLIC sarusaru_aot sarusaru_savedata sarusaru_atrac sarusaru_gpu sarusaru_text)
    if(SARUSARU_HAS_FFMPEG)
        target_compile_definitions(sarusaru_session PRIVATE SARUSARU_HAS_FFMPEG=1)
    endif()
    add_executable(sarusaru_bootstrap host/bootstrap.cpp)
    target_link_libraries(sarusaru_bootstrap PRIVATE sarusaru_session)
    sarusaru_use_ffmpeg(sarusaru_bootstrap)
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
        add_executable(sarusaru host/shell/main.cpp host/shell/vulkan_context.cpp host/shell/ge_renderer.cpp
            host/shell/audio_output.cpp "${_sarusaru_shaders}" "${_sarusaru_icon}"
            ${_sarusaru_resources})
        if(WIN32)
            target_include_directories(sarusaru PRIVATE
                "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}/resources>")
        endif()
        target_link_libraries(sarusaru PRIVATE sarusaru_session sarusaru_shell sarusaru_controls)
        sarusaru_use_ffmpeg(sarusaru)
        set_target_properties(sarusaru PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            BUILD_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path/lib,$ORIGIN/lib>")
        if(PSPRECOMP_BUILD_PROFILE_TESTS)
            add_test(NAME sarusaru_shell_help COMMAND sarusaru --help)
        endif()
    endif()
    if(PSPRECOMP_BUILD_PROFILE_TESTS)
        add_test(NAME sarusaru_bootstrap_help COMMAND sarusaru_bootstrap --help)
        add_test(NAME sarusaru_bootstrap_missing_input COMMAND sarusaru_bootstrap)
        set_tests_properties(sarusaru_bootstrap_missing_input PROPERTIES WILL_FAIL TRUE)
    endif()
endif()
