# Android app

The Android port is SDL's `SDLActivity` around the port's shell, built as
`libmain.so` from the repository's CMake project with the NDK. Like the
desktop packages, the APK holds the launcher and its libraries, never the
game, which the launcher builds on the phone from the player's disc, with a
compiler the APK ships (below).

## What is here

- `app/build.gradle.kts`: the app, arm64-v8a only, Android 10 (API 29) and
  later. The native half is the root `CMakeLists.txt` with
  `PSPRECOMP_PROFILE=sarusaru` and `ANDROID_STL=c++_shared` (one C++ runtime
  for `libmain.so`, `libpsprecomp_runtime.so` and, later, the game module).
  SDL's Java sources come from the same pinned, hash-checked SDL release as
  its C sources (`cmake/Graphics.cmake`); the icon is `include/image/logo.png`.
- `SaruSaruActivity.java`: copies the APK's `resources/` assets (fonts,
  licences, the title) to `no_backup/shipped/` once per installed version,
  since the port opens them as files, and passes the `args` string-array extra
  as the command line.
- `res/xml/game_mode_config.xml`: Android 13+ game modes, without system
  downscaling or frame-rate overrides.

Settings and the port's data live in the app's private `files/` directory.
`stdout` and `stderr` go to the system log under the tag `sarusaru`.

## Build

Needs a JDK 17 or newer, the Android SDK (platform 36), NDK 29.0.14206865 and
the SDK's CMake 4.1.2 (Gradle installs missing SDK parts whose licences are
accepted), plus `glslang` on `PATH` for the shaders and `make` for FFmpeg. On
macOS:

```bash
brew install --cask android-commandlinetools
brew install glslang
sdkmanager --sdk_root="$HOME/Library/Android/sdk" "platforms;android-36" "ndk;29.0.14206865" "cmake;4.1.2"
```

Then, with `sdk.dir` in `local.properties` or `ANDROID_HOME` set:

```bash
cd profiles/sarusaru/packaging/android
./gradlew assembleDebug
```

The APK is `app/build/outputs/apk/debug/app-debug.apk`. The first build
cross-compiles FFmpeg, as the desktop builds do. Release builds are unsigned
until the release policy settles signing.

## The compiler the app ships

The app builds the game as the desktop launchers do: the code generator
(`libpsp_recomp.so`) and clang and lld (one LLVM program,
`libsarusaru_llvm.so`) run as programs from the APK's native library
directory, the only place Android 10 and later let an app run them, and the
module is linked against the phone's own C library and the APK's
`libc++_shared.so`. The headers and link files they need are `sdk.zip`
among the assets, unpacked by the activity.

`toolchain/build_toolchain.py` builds all of that from pinned source into
`out/android-toolchain/stage/`, which the Gradle build picks up (without it
the APK builds but cannot install the game):

```bash
brew install gpatch
python3 profiles/sarusaru/packaging/android/toolchain/build_toolchain.py
```

- clang, lld, clang's headers, libc++'s headers, compiler-rt's builtins and
  libunwind: llvm-project at the revision the NDK r29 clang is based on, with
  the same 46 Android patches (`toolchain/llvm_android` at the commit its
  `clang_source_info.md` names). libc++'s headers come out identical to the
  NDK's, so the module and `libc++_shared.so` agree on the C++ ABI.
- The C library's headers and the shared-object start and end files: bionic
  at NDK r29's revision.

Nothing is copied from the NDK; it is only the compiler that builds these.
The LLVM build takes about an hour on an M-series Mac.

## Play before the app can install the game

Until the on-device build exists, build the game module on this computer
from your own disc and push it to the phone (USB debugging on, the debug APK
installed):

```bash
python3 profiles/sarusaru/scripts/android_module.py --iso /path/to/game.iso
```

It runs the installer's steps with the NDK's clang (the code generator from
`out/macos-arm64`, the runtime and SDK of the APK's own build, so the
interface hash matches) and copies `sarusaru_game.so` into the app's
`files/game/<hash>/`. The generated code and the module stay in
`out/android-module/` and on the phone; they are never packaged.

## Run

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n io.github.sarusaru_recomp.SaruSaruRecomp/io.github.sarusaru_recomp.SaruSaruActivity --esa args --scale,2
adb logcat -s sarusaru SDL
```
