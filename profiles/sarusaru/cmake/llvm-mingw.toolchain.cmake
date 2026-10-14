# Builds the Windows x64 port with llvm-mingw (fetch_llvm_mingw.cmake), on
# Windows or cross-compiling from Linux:
#   cmake -DCMAKE_TOOLCHAIN_FILE=profiles/sarusaru/cmake/llvm-mingw.toolchain.cmake
#         -DLLVM_MINGW_ROOT=<directory>/llvm-mingw ...
# Releases use it so that a player's launcher, which downloads the same
# llvm-mingw, builds a game module that matches the port.
if(NOT LLVM_MINGW_ROOT AND DEFINED ENV{LLVM_MINGW_ROOT})
    set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
endif()
if(NOT LLVM_MINGW_ROOT)
    message(FATAL_ERROR "Set LLVM_MINGW_ROOT to the llvm-mingw directory")
endif()
set(LLVM_MINGW_ROOT "${LLVM_MINGW_ROOT}" CACHE PATH "The llvm-mingw toolchain")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_ROOT)

if(NOT CMAKE_HOST_WIN32)
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
endif()
set(_suffix "")
if(CMAKE_HOST_WIN32)
    set(_suffix ".exe")
endif()
set(CMAKE_C_COMPILER "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang${_suffix}")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++${_suffix}")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-windres${_suffix}")
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
