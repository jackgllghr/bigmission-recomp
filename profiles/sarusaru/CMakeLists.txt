find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(PSPRECOMP_BUILD_PROFILE_TESTS)
    enable_testing()
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
        tests/umd_tests.cpp host/umd/disc_image.cpp host/umd/umd_drive.cpp)
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
    # Keep initial diagnostic compilation inexpensive; optimize after correctness.
    if(NOT MSVC)
        target_compile_options(sarusaru_aot PRIVATE -O0)
    endif()

    add_executable(sarusaru_bootstrap host/bootstrap.cpp host/startup_hle.cpp host/kernel/early_kernel.cpp
        host/kernel/sync.cpp host/umd/disc_image.cpp host/umd/umd_drive.cpp host/ctrl/controller.cpp
        host/display/display.cpp)
    target_include_directories(sarusaru_bootstrap PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/host")
    target_link_libraries(sarusaru_bootstrap PRIVATE sarusaru_aot)
    set_target_properties(sarusaru_bootstrap PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(PSPRECOMP_BUILD_PROFILE_TESTS)
        add_test(NAME sarusaru_bootstrap_help COMMAND sarusaru_bootstrap --help)
        add_test(NAME sarusaru_bootstrap_missing_input COMMAND sarusaru_bootstrap)
        set_tests_properties(sarusaru_bootstrap_missing_input PROPERTIES WILL_FAIL TRUE)
    endif()
endif()
