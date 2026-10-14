# Window, input, audio output and Vulkan for the host shell: SDL3, the Vulkan
# headers, glslang (to compile the GE shaders at build time) and, on macOS,
# MoltenVK. All are pinned and hash-checked downloads; nothing is taken from
# the system except the Vulkan driver on Linux, Windows and Android.
#
# Vulkan is never linked: the shell loads its functions at run time through
# SDL (SDL_Vulkan_GetVkGetInstanceProcAddr), from MoltenVK on macOS and from
# the system loader elsewhere.
#
# Sets SARUSARU_HAS_RENDERER and SARUSARU_GLSLANG (the compiler target).

include(FetchContent)

set(SARUSARU_SDL3_VERSION 3.4.16)
set(SARUSARU_SDL3_URL "https://github.com/libsdl-org/SDL/releases/download/release-${SARUSARU_SDL3_VERSION}/SDL3-${SARUSARU_SDL3_VERSION}.tar.gz")
set(SARUSARU_SDL3_SHA256 7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68)

set(SARUSARU_VULKAN_HEADERS_TAG vulkan-sdk-1.4.357.0)
set(SARUSARU_VULKAN_HEADERS_URL "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/${SARUSARU_VULKAN_HEADERS_TAG}.tar.gz")
set(SARUSARU_VULKAN_HEADERS_SHA256 e87dce08116151f6b6d7de6b6faf41498e87e6cf848ff16fa3bd5402190ad4a3)

set(SARUSARU_GLSLANG_VERSION 16.5.0)
set(SARUSARU_GLSLANG_URL "https://github.com/KhronosGroup/glslang/archive/refs/tags/${SARUSARU_GLSLANG_VERSION}.tar.gz")
set(SARUSARU_GLSLANG_SHA256 01af17195fbeb59e39e31e9506de35bb39dfd35807ea0c9a1a99d7d1183ddd45)

set(SARUSARU_MOLTENVK_VERSION 1.4.2)
set(SARUSARU_MOLTENVK_URL "https://github.com/KhronosGroup/MoltenVK/releases/download/v${SARUSARU_MOLTENVK_VERSION}/MoltenVK-macos.tar")
set(SARUSARU_MOLTENVK_SHA256 f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e)

option(SARUSARU_RENDERER "Build the windowed host shell (SDL3 + Vulkan)" ON)
set(SARUSARU_HAS_RENDERER OFF)
if(NOT SARUSARU_RENDERER)
    return()
endif()

# SDL3 as a shared library next to the executable.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sarusaru_sdl3 URL "${SARUSARU_SDL3_URL}" URL_HASH SHA256=${SARUSARU_SDL3_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_Declare(sarusaru_vulkan_headers URL "${SARUSARU_VULKAN_HEADERS_URL}"
    URL_HASH SHA256=${SARUSARU_VULKAN_HEADERS_SHA256} DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# glslang only as the standalone compiler, without the SPIR-V optimizer.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sarusaru_glslang URL "${SARUSARU_GLSLANG_URL}" URL_HASH SHA256=${SARUSARU_GLSLANG_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(sarusaru_sdl3 sarusaru_vulkan_headers sarusaru_glslang)
set_target_properties(SDL3-shared PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/lib" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(SARUSARU_GLSLANG glslang-standalone)

if(APPLE)
    FetchContent_Declare(sarusaru_moltenvk URL "${SARUSARU_MOLTENVK_URL}" URL_HASH SHA256=${SARUSARU_MOLTENVK_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(sarusaru_moltenvk)
    # The archive is a prebuilt dylib, not a CMake project: place it next to
    # the executable with its licence.
    file(GLOB_RECURSE _sarusaru_mvk_dylib "${sarusaru_moltenvk_SOURCE_DIR}/*/dynamic/dylib/macOS/libMoltenVK.dylib"
        "${sarusaru_moltenvk_SOURCE_DIR}/dynamic/dylib/macOS/libMoltenVK.dylib")
    file(GLOB_RECURSE _sarusaru_mvk_license "${sarusaru_moltenvk_SOURCE_DIR}/LICENSE")
    list(GET _sarusaru_mvk_dylib 0 _sarusaru_mvk_dylib)
    configure_file("${_sarusaru_mvk_dylib}" "${CMAKE_BINARY_DIR}/bin/lib/libMoltenVK.dylib" COPYONLY)
    if(_sarusaru_mvk_license)
        list(GET _sarusaru_mvk_license 0 _sarusaru_mvk_license)
        configure_file("${_sarusaru_mvk_license}" "${CMAKE_BINARY_DIR}/bin/lib/MoltenVK-LICENSE.txt" COPYONLY)
    endif()
endif()

set(SARUSARU_HAS_RENDERER ON)
message(STATUS "sarusaru: host shell with SDL3 ${SARUSARU_SDL3_VERSION}, Vulkan headers ${SARUSARU_VULKAN_HEADERS_TAG}, "
    "glslang ${SARUSARU_GLSLANG_VERSION}")
