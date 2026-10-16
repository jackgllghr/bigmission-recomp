# PPSSPP 1.20.4 writes Zstandard-compressed state containers. Bundle the
# permissively licensed codec so importing those files works on every host.
include(FetchContent)
enable_language(C)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sarusaru_zstd
    URL "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz"
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    SOURCE_SUBDIR build/cmake
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sarusaru_zstd)
set_target_properties(libzstd_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
configure_file("${sarusaru_zstd_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/bin/resources/Zstandard-LICENSE.txt" COPYONLY)

add_library(sarusaru_ppst STATIC host/save_state/slots.cpp host/save_state/ppst.cpp host/save_state/machine.cpp host/save_state/kernel_state.cpp
    host/save_state/ppsspp_objects.cpp host/save_state/ppsspp_scheduler.cpp host/save_state/ppsspp_thread.cpp
    host/save_state/ppsspp_sync.cpp host/save_state/ppsspp_syscalls.cpp host/save_state/kernel_snapshot.cpp)
target_include_directories(sarusaru_ppst PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_link_libraries(sarusaru_ppst PRIVATE libzstd_static PUBLIC psprecomp_runtime)

add_executable(sarusaru_ppst_tool tools/ppst_main.cpp)
set_target_properties(sarusaru_ppst_tool PROPERTIES OUTPUT_NAME sarusaru_ppst)
target_link_libraries(sarusaru_ppst_tool PRIVATE sarusaru_ppst)

if(PSPRECOMP_BUILD_PROFILE_TESTS)
    enable_testing()
    add_executable(sarusaru_ppst_tests tests/ppst_tests.cpp)
    target_link_libraries(sarusaru_ppst_tests PRIVATE sarusaru_ppst)
    add_test(NAME sarusaru_ppst_tests COMMAND sarusaru_ppst_tests)
    add_executable(sarusaru_ppst_machine_tests tests/ppst_machine_tests.cpp)
    target_link_libraries(sarusaru_ppst_machine_tests PRIVATE sarusaru_ppst)
    add_test(NAME sarusaru_ppst_machine_tests COMMAND sarusaru_ppst_machine_tests)
    add_executable(sarusaru_ppst_kernel_tests tests/ppst_kernel_tests.cpp)
    target_link_libraries(sarusaru_ppst_kernel_tests PRIVATE sarusaru_ppst)
    add_test(NAME sarusaru_ppst_kernel_tests COMMAND sarusaru_ppst_kernel_tests)
    add_executable(sarusaru_kernel_snapshot_tests tests/kernel_snapshot_tests.cpp host/kernel/early_kernel.cpp
        host/kernel/sync.cpp host/kernel/interrupts.cpp)
    target_link_libraries(sarusaru_kernel_snapshot_tests PRIVATE sarusaru_ppst)
    add_test(NAME sarusaru_kernel_snapshot_tests COMMAND sarusaru_kernel_snapshot_tests)
    set(SARUSARU_PPSSPP_FIXTURE "" CACHE FILEPATH "User-owned PPSSPP 1.20.4 state for machine-schema verification")
    if(SARUSARU_PPSSPP_FIXTURE)
        add_test(NAME sarusaru_ppst_reference_roundtrip COMMAND sarusaru_ppst_tool verify "${SARUSARU_PPSSPP_FIXTURE}")
        add_test(NAME sarusaru_ppst_reference_objects COMMAND sarusaru_ppst_tool objects "${SARUSARU_PPSSPP_FIXTURE}")
        add_test(NAME sarusaru_ppst_reference_scheduler COMMAND sarusaru_ppst_tool scheduler "${SARUSARU_PPSSPP_FIXTURE}")
    endif()
endif()
