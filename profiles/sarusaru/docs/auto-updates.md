# Release versions and update distribution

## Implemented phase

The launcher has an Updates tab with its installed version, a daily background
check preference (on by default), Check Now and a release-download link. Checks
run off the UI thread, have a 20-second deadline and can be cancelled at shutdown.
A failed/offline check does not prevent playing. Only a strictly newer stable
version for the current architecture is offered. No desktop package is executed
or replaced by this phase; installation remains manual. Flatpak directs players
to their software manager.

Android's Updates tab links directly to Obtainium. The tag release workflow now
builds a signed arm64 APK, including the on-device compiler and SDK, and attaches
it alongside the desktop packages. Obtainium handles APK updates externally.
Whether installation can run silently depends on the Android device and Obtainium
settings; see [Obtainium's tracking documentation](https://wiki.obtainium.imranr.dev/app_tracking/).

Sparkle/WinSparkle installation, AppImage self-installation and a hosted Flatpak
update repository remain subsequent phases of the
[investigation](auto-updates-research.md). They are not enabled by these checks.

## Version identity

`profiles/sarusaru/VERSION` is the development/default release identity. For a
release, tag `vX.Y.Z` is authoritative. The workflow validates it and supplies
`SARUSARU_RELEASE_VERSION=X.Y.Z` to CMake and `-PreleaseVersion=X.Y.Z` to Gradle.
The same identity appears in the executable (`--version`), Updates tab, macOS
bundle, package filenames, Android `versionName`, and desktop update feed.
Packaging rejects an explicit version that differs from the built launcher.

Only stable numeric versions are supported in this phase. Android's version code
is `major * 1,000,000 + minor * 1,000 + patch + 1`, with minor and patch limited
to 999 and a maximum result of 2,100,000,000. Thus every increasing supported
stable version has an increasing version code. Do not reuse a published version
for a different APK. Preview channels require a separate version-code policy.

This version does **not** change `PROJECT_VERSION` or the generated-game ABI hash.
The existing public-header/code-generator compatibility inputs remain intact.
When those change, the launcher still rebuilds the game from the player's ISO;
UI-only release version changes do not invalidate the cache. Settings and saves
remain in the existing per-user data directory.

## Configure Android signing before the first release

Create a permanent release keystore with Android's standard signing tools. Keep
a secure backup outside the repository, and retain its certificate identity for
all future APKs. See [Android app signing](https://developer.android.com/studio/publish/app-signing).
Configure these GitHub Actions repository secrets:

| Secret | Value |
| --- | --- |
| `ANDROID_KEYSTORE_BASE64` | Base64 encoding of the binary release keystore |
| `ANDROID_STORE_PASSWORD` | Keystore password |
| `ANDROID_KEY_ALIAS` | Signing-key alias |
| `ANDROID_KEY_PASSWORD` | Signing-key password |

Do not commit the keystore, its base64 encoding or passwords. The job decodes the
keystore into the runner's temporary directory, uses restrictive permissions,
verifies the signed APK with `apksigner`, and removes the temporary key afterward.
Missing credentials fail the release job; an unsigned APK is never substituted.

Local release builds use these environment variables (not Gradle command-line
passwords): `SARUSARU_ANDROID_KEYSTORE` (absolute path),
`SARUSARU_ANDROID_STORE_PASSWORD`, `SARUSARU_ANDROID_KEY_ALIAS`, and
`SARUSARU_ANDROID_KEY_PASSWORD`. Then run:

```sh
cd profiles/sarusaru/packaging/android
./gradlew assembleRelease -PreleaseVersion=0.2.0
```

The existing app ID is preserved. A release key cannot update an existing
installation signed with a different debug key. Export saves/settings before
uninstalling such a build; reinstalling loses Android private app data. Test this
migration on a device before distributing the first production release.

## Obtainium onboarding

Install the signed release APK, then use **Settings → Updates → Add to Obtainium**,
or add `https://github.com/jackgllghr/bigmission-recomp` in Obtainium. The launcher
uses the documented `obtainium://add/` deep link and offers Obtainium's website
if Android cannot handle it. A release contains exactly one APK, so no variant
filter is needed. Keep debug APKs as workflow artifacts rather than release
assets. See [Obtainium deep links](https://wiki.obtainium.imranr.dev/deep_links/).

## Publishing a stable update

1. Configure signing secrets and verify a release build on an Android device.
2. Choose a new `X.Y.Z`; update `profiles/sarusaru/VERSION` for development builds.
3. Push tag `vX.Y.Z`. Release CI builds/tests the desktop packages and signed APK.
   The Android compiler is built from the existing pinned sources on a cold cache;
   subsequent runs cache its staged files. This cold build can be substantial.
4. CI checks that the APK contains the compiler/SDK and no game-derived module,
   ELF, ISO or translation pack. It verifies the APK signature and constructs
   `update-info.txt` only when every supported desktop asset exists.
5. Review and publish the draft GitHub release. Before publication desktop checks
   and Obtainium continue to see the previous published stable release.
6. Verify N→N+1 in Obtainium, desktop version detection, per-user data preservation,
   and a required game rebuild when ABI inputs actually change.

The small desktop feed is fetched from GitHub's documented
[latest-release asset URL](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases):
`https://github.com/jackgllghr/bigmission-recomp/releases/latest/download/update-info.txt`.
It contains a schema header, stable version, and exact versioned download URLs
for Windows x64, macOS arm64, and Linux x64/arm64. The parser restricts links to
this repository's matching platform packages and rejects malformed, duplicate,
truncated or oversized data. It does not use the GitHub API or need an API token.
The file is **check-only metadata**, not an authenticated executable update
manifest. Future automatic installers must independently verify signed payloads
and support failed-install recovery.

## Validation and remaining release checks

Automated tests cover numeric version comparison, no downgrades, unsupported
platforms, invalid/duplicate/truncated feeds, foreign URLs, clock changes, settings
round trips, complete release asset selection and APK contents. Gradle configuration
can be checked with `./gradlew help` without release credentials.

A signed APK and the release workflow require the signing secrets and CI runners;
local macOS verification does not prove Windows/Linux installation or Android
upgrade behavior. Those checks, and the first published release feed, must be
validated on their target systems before describing this phase as live.
