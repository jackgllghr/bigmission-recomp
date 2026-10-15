# Android app

The Android port is SDL's `SDLActivity` around the port's shell, built as
`libmain.so` from the repository's CMake project with the NDK. Like the
desktop packages, the APK holds the launcher and its libraries, never the
game: building the game module on the device (the code generator, clang and
lld in-process) is the next step of the Android chunk in
[`PLAN.md`](../../../../PLAN.md). Until then the launcher opens but cannot
install the game.

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

## Run

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n io.github.sarusaru_recomp.SaruSaruRecomp/io.github.sarusaru_recomp.SaruSaruActivity --esa args --scale,2
adb logcat -s sarusaru SDL
```
