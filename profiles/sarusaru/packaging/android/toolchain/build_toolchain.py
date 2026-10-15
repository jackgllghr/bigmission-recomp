#!/usr/bin/env python3
"""Build the compiler the Android app ships to build the game on the phone.

Everything comes from pinned source, nothing from the NDK's prebuilt files:

- clang and lld, as one LLVM multicall program (`llvm clang++ ...`,
  `llvm ld.lld ...`), from llvm-project at the revision the NDK's own clang
  is built from, with the same Android patches, so the module and the APK
  agree on the C++ ABI;
- from the same source: clang's own headers, libc++'s and libc++abi's
  headers configured as the NDK's libc++_shared.so is (the __ndk1 ABI
  namespace), and compiler-rt's builtins and libunwind for linking;
- from bionic at the NDK's revision: the C library's headers and the
  shared-object start and end files.

The C library itself (libc, libm, libdl) is not shipped: the phone's own is
linked against when the game is built. The NDK is only the compiler that
builds all this.

Output, in out/android-toolchain/stage/ (picked up by the Gradle build):
  jni/libsarusaru_llvm.so   the multicall program, an executable named as a
                            library so the APK installs it where Android lets
                            an app run programs
  sysroot/                  headers and link files, shipped as assets
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[5]
WORK = ROOT / "out/android-toolchain"
STAGE = WORK / "stage"

# The NDK r29 clang's base (its clang_source_info.md) and NDK r29's bionic.
LLVM_REVISION = "386af4a5c64ab75eaee2448dc38f2e34a40bfed0"
LLVM_URL = f"https://github.com/llvm/llvm-project/archive/{LLVM_REVISION}.tar.gz"
LLVM_SHA256 = "4b752a2dfb5e4e55cf9812a353bce7e437e0852f4eef64929f48d74732080c50"
LLVM_MAJOR = "21"
# Android's patches on that base, as the NDK's clang has them: llvm_android at
# the commit its clang_source_info.md names, every patch for Android whose
# version range holds the clang's revision (r563880).
LLVM_ANDROID_URL = "https://android.googlesource.com/toolchain/llvm_android"
LLVM_ANDROID_REVISION = "1dab3288f660d43a6cb2479107e2b54b3ab0a2a1"
LLVM_ANDROID_SVN_REVISION = 563880
BIONIC_URL = "https://android.googlesource.com/platform/bionic"
BIONIC_REVISION = "a63b21091ada240d92f8ceadb1cabbdedaaab81b"
NDK_VERSION = "29.0.14206865"
API_LEVEL = 29
TRIPLE = f"aarch64-linux-android{API_LEVEL}"

# Tools that would otherwise join the multicall program: only clang and lld are wanted.
UNWANTED_LLVM_TOOLS = ["dsymutil", "llvm-ar", "llvm-cgdata", "llvm-cxxfilt", "llvm-debuginfod",
                       "llvm-debuginfod-find", "llvm-dwp", "llvm-gsymutil", "llvm-ifs", "llvm-libtool-darwin",
                       "llvm-lipo", "llvm-ml", "llvm-mt", "llvm-nm", "llvm-objcopy", "llvm-objdump",
                       "llvm-profdata", "llvm-rc", "llvm-readobj", "llvm-size", "llvm-symbolizer", "sancov"]
UNWANTED_CLANG_TOOLS = ["clang-installapi", "clang-scan-deps"]


def run(*command, **options):
    print("+", " ".join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], check=True, **options)


def option_name(tool):
    return tool.upper().replace("-", "_")


def ndk_directory(sdk):
    ndk = sdk / "ndk" / NDK_VERSION
    if not (ndk / "build/cmake/android.toolchain.cmake").is_file():
        raise SystemExit(f"build_toolchain: NDK {NDK_VERSION} not found in {sdk}")
    return ndk


def ndk_bin(ndk):
    for host in ("darwin-x86_64", "linux-x86_64"):
        directory = ndk / "toolchains/llvm/prebuilt" / host / "bin"
        if directory.is_dir():
            return directory
    raise SystemExit("build_toolchain: the NDK has no prebuilt toolchain for this host")


def llvm_source():
    archive = WORK / "downloads" / f"llvm-project-{LLVM_REVISION}.tar.gz"
    source = WORK / "src" / f"llvm-project-{LLVM_REVISION}"
    if source.is_dir():
        return source
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != LLVM_SHA256:
        archive.parent.mkdir(parents=True, exist_ok=True)
        partial = archive.with_suffix(".part")
        print(f"Downloading {LLVM_URL}", flush=True)
        with urllib.request.urlopen(LLVM_URL) as response, partial.open("wb") as out:
            shutil.copyfileobj(response, out)
        actual = hashlib.sha256(partial.read_bytes()).hexdigest()
        if actual != LLVM_SHA256:
            partial.unlink()
            raise SystemExit(f"build_toolchain: {LLVM_URL} has SHA-256 {actual}, expected {LLVM_SHA256}")
        partial.rename(archive)
    (WORK / "src").mkdir(parents=True, exist_ok=True)
    run("tar", "-xzf", archive, "-C", WORK / "src")
    return source


def git_checkout(url, revision, directory):
    """`directory` at exactly `revision`, fetched alone."""
    head = subprocess.run(["git", "-C", str(directory), "rev-parse", "HEAD"], capture_output=True, text=True)
    if head.returncode == 0 and head.stdout.strip() == revision:
        return directory
    shutil.rmtree(directory, ignore_errors=True)
    directory.mkdir(parents=True)
    run("git", "-C", directory, "init", "-q")
    run("git", "-C", directory, "fetch", "-q", "--depth", "1", url, revision)
    run("git", "-C", directory, "checkout", "-q", "FETCH_HEAD")
    return directory


def bionic_source():
    return git_checkout(BIONIC_URL, BIONIC_REVISION, WORK / "bionic")


def apply_android_patches(source):
    """Android's patches to llvm-project, once, in their file's order."""
    stamp = source / ".android-patches"
    if stamp.is_file() and stamp.read_text(encoding="utf-8").strip() == LLVM_ANDROID_REVISION:
        return
    if stamp.exists():
        raise SystemExit(f"build_toolchain: {source} has other patches; delete it to unpack it again")
    # GNU patch, as Android's patch manager uses (macOS's own patch applies some differently).
    gnu_patch = next((tool for tool in ("gpatch", "patch") if shutil.which(tool) and "GNU" in subprocess.run(
        [tool, "--version"], capture_output=True, text=True).stdout), None)
    if gnu_patch is None:
        raise SystemExit("build_toolchain: needs GNU patch (brew install gpatch)")
    patches = git_checkout(LLVM_ANDROID_URL, LLVM_ANDROID_REVISION, WORK / "llvm_android") / "patches"
    entries = json.loads((patches / "PATCHES.json").read_text(encoding="utf-8"))
    for entry in entries:
        versions = entry["version_range"]
        if "android" not in entry["platforms"] or versions.get("from") is None:
            continue
        if versions["from"] <= LLVM_ANDROID_SVN_REVISION and (
                versions.get("until") is None or LLVM_ANDROID_SVN_REVISION < versions["until"]):
            run(gnu_patch, "-p1", "--forward", "--quiet", "--no-backup-if-mismatch", "-d", source,
                "-i", patches / entry["rel_patch_path"])
    stamp.write_text(LLVM_ANDROID_REVISION + "\n", encoding="utf-8")


def android_cmake(ndk, *extra):
    return ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}",
            "-DANDROID_ABI=arm64-v8a", f"-DANDROID_PLATFORM=android-{API_LEVEL}", *extra]


def build_llvm(ndk, source, jobs):
    """clang and lld for the phone, as one program."""
    build = WORK / "build-llvm"
    options = android_cmake(ndk,
        # A standalone program: its own static C++ library. The NDK compiles
        # with -g; stripping at the link keeps the output (and the disk
        # it needs) small.
        "-DANDROID_STL=c++_static", "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--strip-all",
        "-DLLVM_ENABLE_PROJECTS=clang;lld", "-DLLVM_TARGETS_TO_BUILD=AArch64",
        f"-DLLVM_DEFAULT_TARGET_TRIPLE={TRIPLE}", "-DLLVM_HOST_TRIPLE=aarch64-linux-android",
        "-DLLVM_TOOL_LLVM_DRIVER_BUILD=ON",
        # The source is unpacked inside this repository: keep its git
        # revision and URL out of the program's version.
        "-DLLVM_APPEND_VC_REV=OFF",
        "-DLLVM_ENABLE_ASSERTIONS=OFF", "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_ZSTD=OFF",
        "-DLLVM_ENABLE_LIBXML2=OFF", "-DLLVM_ENABLE_TERMINFO=OFF", "-DLLVM_ENABLE_LIBEDIT=OFF",
        "-DLLVM_ENABLE_LIBPFM=OFF", "-DLLVM_ENABLE_BINDINGS=OFF", "-DLLVM_ENABLE_CURL=OFF",
        "-DLLVM_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_EXAMPLES=OFF", "-DLLVM_INCLUDE_BENCHMARKS=OFF",
        "-DLLVM_INCLUDE_DOCS=OFF", "-DLLVM_INCLUDE_UTILS=OFF",
        "-DCLANG_INCLUDE_TESTS=OFF", "-DCLANG_INCLUDE_DOCS=OFF", "-DCLANG_ENABLE_ARCMT=OFF",
        "-DCLANG_ENABLE_STATIC_ANALYZER=OFF", "-DCLANG_PLUGIN_SUPPORT=OFF",
        "-DLLD_BUILD_TOOLS=ON",
        *(f"-DLLVM_TOOL_{option_name(tool)}_BUILD=OFF" for tool in UNWANTED_LLVM_TOOLS),
        *(f"-DCLANG_TOOL_{option_name(tool)}_BUILD=OFF" for tool in UNWANTED_CLANG_TOOLS))
    run("cmake", "-S", source / "llvm", "-B", build, *options)
    run("cmake", "--build", build, "--parallel", jobs, "--target", "llvm-driver", "clang-resource-headers")
    return build


def shipped_code_flags():
    """For code that ships: source paths (in __FILE__) relative to the work directory, not this machine's."""
    flags = f"-ffile-prefix-map={WORK}=."
    return [f"-DCMAKE_C_FLAGS={flags}", f"-DCMAKE_CXX_FLAGS={flags}", f"-DCMAKE_ASM_FLAGS={flags}"]


def build_runtimes(ndk, source, jobs):
    """libc++'s headers (as the NDK configures libc++), builtins and libunwind for the module."""
    build = WORK / "build-runtimes"
    options = android_cmake(ndk, "-DANDROID_STL=none", *shipped_code_flags(),
        "-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi;libunwind", "-DLLVM_INCLUDE_TESTS=OFF",
        # As the NDK's libc++_shared.so: its ABI namespace and C++ library, libc++abi beneath.
        "-DLIBCXX_ABI_NAMESPACE=__ndk1", "-DLIBCXX_CXX_ABI=libcxxabi", "-DLIBCXX_HAS_MUSL_LIBC=OFF",
        "-DLIBCXX_INCLUDE_BENCHMARKS=OFF", "-DLIBCXX_ENABLE_ASSERTIONS=OFF",
        "-DLIBUNWIND_ENABLE_SHARED=OFF", "-DLIBUNWIND_USE_COMPILER_RT=ON",
        f"-DCMAKE_INSTALL_PREFIX={WORK / 'runtimes'}")
    run("cmake", "-S", source / "runtimes", "-B", build, *options)
    run("cmake", "--build", build, "--parallel", jobs, "--target", "generate-cxx-headers", "unwind_static")
    builtins = WORK / "build-builtins"
    run("cmake", "-S", source / "compiler-rt/lib/builtins", "-B", builtins,
        *android_cmake(ndk, "-DANDROID_STL=none", *shipped_code_flags(), "-DCOMPILER_RT_BAREMETAL_BUILD=OFF",
                       f"-DCMAKE_C_COMPILER_TARGET={TRIPLE}", "-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON"))
    run("cmake", "--build", builtins, "--parallel", jobs)
    return build, builtins


def build_crt(ndk, bionic, out):
    """Bionic's start and end files for a shared object."""
    clang = ndk_bin(ndk) / "clang"
    common = bionic / "libc/arch-common/bionic"
    out.mkdir(parents=True, exist_ok=True)
    flags = [f"--target={TRIPLE}", "-O2", "-fPIC", "-DPLATFORM_SDK_VERSION=29", f"-I{common}",
             f"-I{bionic / 'libc/include'}", f"-I{bionic / 'libc'}"]
    run(clang, *flags, "-c", common / "crtbegin_so.c", "-o", out / "crtbegin_so.o")
    run(clang, *flags, "-c", common / "crtend_so.S", "-o", out / "crtend_so.o")


def stage(ndk, llvm_build, runtimes_build, builtins_build, bionic, source):
    shutil.rmtree(STAGE, ignore_errors=True)
    jni = STAGE / "jni"
    sysroot = STAGE / "sysroot"
    jni.mkdir(parents=True)
    strip = ndk_bin(ndk) / "llvm-strip"
    run(strip, "--strip-all", "-o", jni / "libsarusaru_llvm.so", llvm_build / "bin/llvm")
    # clang's own headers, for the phone's clang (its resource directory).
    resource = sysroot / "lib/clang" / LLVM_MAJOR
    shutil.copytree(llvm_build / "lib/clang" / LLVM_MAJOR / "include", resource / "include")
    # libc++ and libc++abi headers, as configured above.
    shutil.copytree(runtimes_build / "include/c++/v1", sysroot / "usr/include/c++/v1")
    for header in ("cxxabi.h", "__cxxabi_config.h"):
        shutil.copy2(source / "libcxxabi/include" / header, sysroot / "usr/include/c++/v1" / header)
    # Bionic's headers, with the arm64 kernel headers as asm/.
    include = sysroot / "usr/include"
    shutil.copytree(bionic / "libc/include", include, dirs_exist_ok=True)
    for part in ("linux", "asm-generic", "drm", "misc", "mtd", "rdma", "scsi", "sound", "video", "xen"):
        if (bionic / "libc/kernel/uapi" / part).is_dir():
            shutil.copytree(bionic / "libc/kernel/uapi" / part, include / part, dirs_exist_ok=True)
    shutil.copytree(bionic / "libc/kernel/uapi/asm-arm64/asm", include / "asm", dirs_exist_ok=True)
    shutil.copytree(bionic / "libc/kernel/android/uapi", include, dirs_exist_ok=True)
    # Link files: start and end, builtins, the unwinder.
    lib = sysroot / "usr/lib"
    build_crt(ndk, bionic, lib)
    builtins = next(builtins_build.rglob("libclang_rt.builtins*.a"))
    shutil.copy2(builtins, lib / "libclang_rt.builtins.a")
    shutil.copy2(next(runtimes_build.rglob("libunwind.a")), lib / "libunwind.a")
    # Built with the NDK's -g: their debug information names this machine.
    for archive in ("libclang_rt.builtins.a", "libunwind.a"):
        run(strip, "--strip-debug", lib / archive)
    # Their licences, beside them.
    licences = sysroot / "licenses"
    licences.mkdir()
    shutil.copy2(source / "llvm/LICENSE.TXT", licences / "LLVM-LICENSE.txt")
    shutil.copy2(bionic / "libc/NOTICE", licences / "bionic-NOTICE.txt")
    (STAGE / "SOURCE.txt").write_text(
        f"llvm-project {LLVM_REVISION} ({LLVM_URL}, SHA-256 {LLVM_SHA256})\n"
        f"bionic {BIONIC_REVISION} ({BIONIC_URL})\n", encoding="utf-8")
    print(f"Staged {STAGE}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get("ANDROID_HOME",
                        Path.home() / "Library/Android/sdk")))
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--only", choices=["llvm", "runtimes", "stage"], help="Run one stage")
    args = parser.parse_args()
    ndk = ndk_directory(args.sdk)
    source = llvm_source()
    apply_android_patches(source)
    bionic = bionic_source()
    llvm_build, runtimes_build, builtins_build = WORK / "build-llvm", WORK / "build-runtimes", WORK / "build-builtins"
    if args.only in (None, "llvm"):
        build_llvm(ndk, source, args.jobs)
    if args.only in (None, "runtimes"):
        build_runtimes(ndk, source, args.jobs)
    if args.only in (None, "stage"):
        stage(ndk, llvm_build, runtimes_build, builtins_build, bionic, source)


if __name__ == "__main__":
    sys.exit(main())
