# llvm-mingw (https://github.com/mstorsjo/llvm-mingw), the toolchain the
# Windows port is released with and the one a player's launcher downloads to
# build the game module, so that the two match. Pinned and hash-checked.
set(SARUSARU_LLVM_MINGW_VERSION 20260922)
set(SARUSARU_LLVM_MINGW_BASE "https://github.com/mstorsjo/llvm-mingw/releases/download/${SARUSARU_LLVM_MINGW_VERSION}")
# The Windows-hosted build, for players.
set(SARUSARU_LLVM_MINGW_URL "${SARUSARU_LLVM_MINGW_BASE}/llvm-mingw-${SARUSARU_LLVM_MINGW_VERSION}-ucrt-x86_64.zip")
set(SARUSARU_LLVM_MINGW_SHA256 e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666)
set(SARUSARU_LLVM_MINGW_SIZE 190725905)
# Linux-hosted builds, for cross-compiling the Windows port.
set(SARUSARU_LLVM_MINGW_LINUX_X64_URL "${SARUSARU_LLVM_MINGW_BASE}/llvm-mingw-${SARUSARU_LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz")
set(SARUSARU_LLVM_MINGW_LINUX_X64_SHA256 bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21)
set(SARUSARU_LLVM_MINGW_LINUX_ARM64_URL "${SARUSARU_LLVM_MINGW_BASE}/llvm-mingw-${SARUSARU_LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-aarch64.tar.xz")
set(SARUSARU_LLVM_MINGW_LINUX_ARM64_SHA256 07d21263c56bfe9a713db6fdb3f7434bf4c121a005e40397d3b4c0170fb06769)
