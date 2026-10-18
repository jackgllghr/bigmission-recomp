# The launcher and the port's own screens: Dear ImGui, and the faces they are
# drawn in. All are pinned and hash-checked downloads, like Graphics.cmake's.
#
# Fonts are placed in bin/resources/fonts/ with their licences, beside the
# executable, and read from there at run time.
#
# Defines the sarusaru_imgui library (with its SDL3 and Vulkan backends).
# Needs Graphics.cmake first (SDL3 and the Vulkan headers).

include(FetchContent)

set(SARUSARU_IMGUI_VERSION 1.92.9b)
set(SARUSARU_IMGUI_URL "https://github.com/ocornut/imgui/archive/refs/tags/v${SARUSARU_IMGUI_VERSION}.tar.gz")
set(SARUSARU_IMGUI_SHA256 21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99)

# google/fonts at a fixed commit, and the M+ project's own licence text
# (google/fonts carries only its METADATA for this family).
set(SARUSARU_FONTS_COMMIT e9678a5385322c63730122ba464765baad83cfd8)
set(SARUSARU_FONTS_URL "https://raw.githubusercontent.com/google/fonts/${SARUSARU_FONTS_COMMIT}")
set(SARUSARU_MPLUS_COMMIT eb604901d6f04b6f7f2a84b0378c58df84a9dba6)
# name | URL | SHA-256
set(SARUSARU_FONT_FILES
    "MPLUSRounded1c-Bold.ttf|${SARUSARU_FONTS_URL}/ofl/mplusrounded1c/MPLUSRounded1c-Bold.ttf|c358630584e8e2d8fbd6121d0f4693255ffef6d1e6d4f3441fd6e5a963a11f9e"
    "MPLUSRounded1c-OFL.txt|https://raw.githubusercontent.com/coz-m/MPLUS_FONTS/${SARUSARU_MPLUS_COMMIT}/OFL.txt|04971e3fcee60b247395150d93b3616f6a0b092572332c96187b472976553abc")

FetchContent_Declare(sarusaru_imgui URL "${SARUSARU_IMGUI_URL}" URL_HASH SHA256=${SARUSARU_IMGUI_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sarusaru_imgui)

# Unmodified Dear ImGui. Vulkan comes through SDL at run time, so the backend
# resolves its functions through the shell's loader (VK_NO_PROTOTYPES).
add_library(sarusaru_imgui STATIC
    "${sarusaru_imgui_SOURCE_DIR}/imgui.cpp"
    "${sarusaru_imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${sarusaru_imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${sarusaru_imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${sarusaru_imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${sarusaru_imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(sarusaru_imgui SYSTEM PUBLIC "${sarusaru_imgui_SOURCE_DIR}" "${sarusaru_imgui_SOURCE_DIR}/backends")
target_compile_definitions(sarusaru_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(sarusaru_imgui PUBLIC SDL3::SDL3 Vulkan::Headers)
configure_file("${sarusaru_imgui_SOURCE_DIR}/LICENSE.txt" "${CMAKE_BINARY_DIR}/bin/resources/DearImGui-LICENSE.txt" COPYONLY)

set(_sarusaru_font_dir "${CMAKE_BINARY_DIR}/bin/resources/fonts")
foreach(entry IN LISTS SARUSARU_FONT_FILES)
    string(REPLACE "|" ";" entry "${entry}")
    list(GET entry 0 name)
    list(GET entry 1 url)
    list(GET entry 2 hash)
    set(destination "${_sarusaru_font_dir}/${name}")
    if(EXISTS "${destination}")
        file(SHA256 "${destination}" existing)
        if(existing STREQUAL hash)
            continue()
        endif()
    endif()
    message(STATUS "sarusaru: downloading ${name}")
    file(DOWNLOAD "${url}" "${destination}" EXPECTED_HASH SHA256=${hash} STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Cannot download ${url}: ${status}")
    endif()
endforeach()

# The title artwork, shown on the launcher's first screen.
configure_file("${PROJECT_SOURCE_DIR}/include/image/SaruSaruBigMission.png"
    "${CMAKE_BINARY_DIR}/bin/resources/title.png" COPYONLY)

message(STATUS "sarusaru: launcher UI with Dear ImGui ${SARUSARU_IMGUI_VERSION}")
