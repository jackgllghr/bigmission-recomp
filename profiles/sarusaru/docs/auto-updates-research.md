# Automatic update investigation

Investigated 2026-10-15. This is a proposal; no updater or release workflow changes
are implemented by this investigation. Implementation progress is documented in
[release/update setup](auto-updates.md).

## Recommendation

GitHub Releases can be the shared distribution source for all four operating
systems. Use platform-specific installation mechanisms behind a small launcher
update interface. Start with release versioning and signed Android APK publishing,
then add desktop update checks, then installation. For a small team, prefer
Sparkle on macOS, AppImageUpdate on Linux AppImage, and WinSparkle with a Windows
installer. Preserve portable Windows ZIP/Linux tarball downloads with a release
page link initially. A portable self-updater is possible but makes the project
responsible for an additional installer, recovery and security implementation.

Android should use **Obtainium** (the project's spelling). It already understands
GitHub releases and manages downloading/installing APKs externally. The game
needs a signed APK release and an onboarding link, rather than its own Android
package installer. [Obtainium](https://github.com/ImranR98/Obtainium)

## Current repository evidence

- [release.yml](../../../.github/workflows/release.yml) builds Windows x64 ZIP,
  Linux x64/arm64 tarballs, AppImages and Flatpak bundles, and macOS arm64 `.app`
  ZIPs. A `v*` tag creates a **draft** release with `SHA256SUMS.txt`; publication
  is manual. There is no Android release job or APK release asset.
- [CMakeLists.txt](../../../CMakeLists.txt) fixes the project version at `0.1.0`.
  The workflow passes its tag to archive packaging, but that alone does not
  establish the installed launcher version. One authoritative release version
  must reach the executable, macOS bundle, Android and updater metadata.
  [The profile CMake file](../CMakeLists.txt) also seeds the game ABI hash with
  `PROJECT_VERSION`: naively bumping it for every UI release forces unnecessary
  local game rebuilds. Separate release/build identity from module compatibility
  identity while preserving the real public-header/code-generator ABI inputs.
- [Android Gradle configuration](../packaging/android/app/build.gradle.kts)
  uses package ID `io.github.sarusaru_recomp.SaruSaruRecomp`, `targetSdk = 36`,
  fixed `versionCode = 1`, and a `versionName` read from CMake. Its release build
  is unsigned; [build.yml](../../../.github/workflows/build.yml) uploads a debug
  APK as a workflow artifact. Debug artifacts are not a stable public release
  channel. The Android toolchain assets must also be present in the release APK.
- [package.py](../scripts/package.py) ad-hoc signs the macOS bundle, without
  Developer ID/notarization. Its AppImage invocation does not embed update
  information or publish `.zsync` files. [build_flatpak.sh](../packaging/linux/build_flatpak.sh)
  exports bundles without an application update repository; Flathub supplies
  runtime dependencies rather than this application.
- [download.cpp](../host/install/download.cpp) provides cancellable process-based
  curl downloading into `.part` files and SHA-256 verification. An updater still
  needs bounded network operations, release parsing, authenticated metadata and
  installation/recovery; dynamically fetched hashes alone do not establish the
  publisher's identity.
- [paths.hpp](../host/platform/paths.hpp) separates per-user settings and data from
  installation files. [game_module.cpp](../host/game_module.cpp) validates locally
  compiled game modules against the host ABI hash. Host updates can require the
  player to rebuild from their own ISO; updates must retain that data and clearly
  explain a necessary rebuild.

## Platform matrix

| Distribution | Proposed updater | Required packaging work | Limits |
| --- | --- | --- | --- |
| Windows x64 installed | WinSparkle + per-user installer | Add installer, appcast, Ed25519 signatures and shutdown callbacks | Current ZIP is not an installer; the installer must implement replacement/relaunch |
| Windows x64 portable ZIP | Initially download-page link; later separate helper | Stage complete package, wait for parent exit, replace/recover, relaunch | Writable location required; no recursive overwrite while the game runs |
| macOS arm64 `.app` | Sparkle 2 | Bundle framework/helpers, feed and public key; sign update archive; evaluate Developer ID/notarization | Test locally generated game dylib with hardened runtime; Intel Mac remains outside current build matrix |
| Linux x64/arm64 AppImage | AppImageUpdate or libappimageupdate | Embed update information and upload matching `.zsync`; enforce trusted signature verification | User-owned writable AppImage; test Steam Deck and launch-path preservation |
| Linux tarball | Initially download-page link; optional portable helper later | Same full-directory staging/recovery problem as Windows portable | Do not assume `/usr` or package-manager installs are writable |
| Linux Flatpak | Flatpak software manager | Publish signed OSTree repository or distribute through Flathub | A standalone bundle does not supply an update stream |
| Android arm64 APK | Obtainium's GitHub source | Publish signed release APK with increasing version code and matching version name | First installation/setup requires user action; silent updates are conditional |

These are proposed choices, informed by [WinSparkle publishing](https://winsparkle.org/guides/publishing-updates/),
[Sparkle integration](https://sparkle-project.org/documentation/),
[AppImage updating](https://docs.appimage.org/packaging-guide/optional/updates.html),
[Flatpak repositories](https://docs.flatpak.org/en/latest/repositories.html),
and [Obtainium tracking](https://wiki.obtainium.imranr.dev/app_tracking/).

### Windows

WinSparkle exposes a C API suitable for the C++ launcher, with its own UI thread.
Ship its DLL, initialize with the application version, appcast URL and Ed25519
public key, and sign each update using the corresponding private key.
[WinSparkle overview](https://winsparkle.org/),
[signing/setup](https://winsparkle.org/guides/getting-started/)

Its usual update enclosure is an MSI, Inno Setup or NSIS installer. It supports
installer arguments and minimum OS/platform selection. It does **not** make the
existing portable ZIP replace itself. Recommended first installed distribution:
a per-user installer that preserves the existing data directories, with the
launcher accepting shutdown only when no stage or local build is running.
Evaluate Authenticode signing for the installer/executable separately from update
archive signatures. [WinSparkle publishing](https://winsparkle.org/guides/publishing-updates/)

### macOS

Sparkle's programmatic controller fits a native launcher through an Objective-C++
bridge. Configure an HTTPS appcast and `SUPublicEDKey`, sign the update archive
with Sparkle's EdDSA key, and ship its correctly signed helpers. Developer ID
signing/notarization is recommended by Sparkle; release CI now does both
(auto-updates.md). Set monotonically increasing bundle/build versions.
[Sparkle documentation](https://sparkle-project.org/documentation/)

The launcher loads a locally compiled `sarusaru_game.dylib`. Apple's hardened
runtime normally permits only Apple or same-Team-ID libraries. **Inference:** this
architecture likely needs `com.apple.security.cs.disable-library-validation` on
the signed host, because the player cannot sign generated code with our Developer
ID. Verify a signed/notarized prototype that installs, builds and loads the local
module, rather than assuming the packaging change is sufficient. Apple notes
additional Gatekeeper scrutiny for this entitlement.
[Apple library validation entitlement](https://developer.apple.com/documentation/BundleResources/Entitlements/com.apple.security.cs.disable-library-validation)

### Linux

AppImage supports embedded update information, an external/bundled AppImageUpdate
tool, or libappimageupdate integration. Use the project's GitHub-release transport
with architecture-specific `.zsync` selection, and preserve stable versus preview
channels. Updating writes a new file beside the original, allowing the old process
to finish. Begin with complete-image updates if delta integration adds excessive
complexity. [AppImage update guide](https://docs.appimage.org/packaging-guide/optional/updates.html)

AppImages support embedded GPG signatures, but merely printing that signature
does not validate it. Explicitly verify signatures against a trusted key before
activation; do not equate `.zsync` hashes with publisher authentication. Validate
the chosen updater's actual enforcement in the integration spike.
[AppImage signing guide](https://docs.appimage.org/packaging-guide/optional/signatures.html)

Flatpak should delegate updates to its software manager. Publish a signed app
repository and `.flatpakref`/`.flatpakrepo`, or use Flathub. A bundle can be linked
to an update remote using `build-bundle --repo-url`; `--runtime-repo` only supplies
dependencies. [Flatpak repositories](https://docs.flatpak.org/en/latest/repositories.html),
[bundle command reference](https://docs.flatpak.org/en/latest/flatpak-command-reference.html#flatpak-build-bundle)

## Android and Obtainium

1. Create and securely back up a permanent release signing key. Keep the package
   ID stable and sign every release compatibly. A new release certificate cannot
   directly upgrade today's debug-signed installation; plan export/migration of
   user data before any uninstall. Android signing-key rotation exists but should
   not be the initial distribution design.
   [Android app signing](https://developer.android.com/studio/publish/app-signing)
2. Derive `versionName` from the release version, and give **each** release a
   strictly increasing `versionCode`. Use an explicit release counter rather than
   `github.run_number` from unrelated workflows. Android uses the code to prevent
   downgrades. [Android versioning](https://developer.android.com/studio/publish/versioning)
3. Attach one release APK, for example `sarusaru-recomp-v0.2.0-android-arm64.apk`,
   to the same public GitHub release as the desktop packages. Exclude debug APKs
   and distinguish any future variants by predictable filenames. Obtainium can
   filter APK names with a regular expression; configure it to select exactly
   one compatible APK. Its GitHub source uses the API and has optional latest-tag
   and release filters. [Obtainium sources](https://wiki.obtainium.imranr.dev/sources/)
4. Publish a README/download-page badge and Android launcher action using
   `obtainium://add/https://github.com/jackgllghr/bigmission-recomp`, or an encoded
   `obtainium://app/<JSON>` configuration containing the real package ID, source
   URL and tested filters. Export the tested configuration from Obtainium rather
   than inventing undocumented `additionalSettings` keys. Offer the documented
   HTTPS redirect fallback for people without Obtainium installed.
   [Obtainium deep links](https://wiki.obtainium.imranr.dev/deep_links/)

Keep tag `v0.2.0` and APK `versionName` `0.2.0` aligned so Obtainium can compare
versions normally. Background installation is best effort: Obtainium documents
Android 12+, recent target SDK, the installed app having been installed by
Obtainium, enabled background updates, and exactly one selected APK. Older or
ineligible devices can still receive update notifications and install with user
interaction. Do not promise universally silent Android updates.
[Obtainium version detection and background updates](https://wiki.obtainium.imranr.dev/app_tracking/)

The activity already refreshes extracted shipped resources using
`versionCode + lastUpdateTime` in
[SaruSaruActivity.java](../packaging/android/app/src/main/java/io/github/sarusaru_recomp/SaruSaruActivity.java).
Test that an APK upgrade preserves private settings/saves, refreshes SDK/assets,
and rebuilds an incompatible cached game module.

## Release contract and launcher behavior

Proposed shared release contract:

- Stable semantic tag, explicit installed version/build number, supported
  platform/architecture/package format, minimum OS and game-module ABI identifier.
- Immutable per-version asset names; complete assets, signatures and metadata
  uploaded to the draft before publication. Publish feeds only after the release
  is public. GitHub immutable releases can lock assets and tags after publication;
  this is a useful optional repository setting, with signatures still verified
  by the updater. [GitHub immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)
- Sparkle/WinSparkle appcasts can reference GitHub-hosted binary assets. Host
  their feed at a stable HTTPS URL, such as GitHub Pages. Obtainium reads the
  GitHub releases directly. Keep feed generation in the release process.

For a lightweight shared check-only implementation, the public endpoint is
`GET https://api.github.com/repos/jackgllghr/bigmission-recomp/releases/latest`.
It excludes drafts/prereleases; asset metadata includes `browser_download_url`,
size and digest. Use explicit version comparison and exact platform selection;
never choose the first archive or GitHub's generated source ZIP. Handle missing
platform assets as unsupported rather than silently substituting another build.
[GitHub releases API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release)

Unauthenticated API access is limited to 60 requests per hour per public IP.
Cache checks, use a long interval such as daily, respect rate-limit retry times,
and let offline failures remain nonblocking. Do not ship a shared GitHub token.
[GitHub rate limits](https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api)

Proposed launcher UI: an Updates area with Check for Updates, automatic-check
preference, installed/latest version, release notes and Update and Restart. Check
asynchronously; offer updates in the launcher and defer installation until the
game/local compiler has stopped. Let users skip a version or disable checks.
Managed distributions should show their update mechanism. Apply only explicitly
selected stable/preview channel updates; reject unintended downgrades.

Any custom portable installer needs signed metadata rooted in an embedded public
key, bounded downloads, safe archive extraction, full-package staging outside
the running tree, free-space checks, process-exit coordination, transactional
replacement/rollback, and relaunch-path preservation. Keep user data outside
the replacement allowlist. A checksum fetched from the same compromised release
as its binary cannot substitute for a signature. These are design requirements
for a future implementation, not capabilities of the current downloader.

## Suggested implementation order and verification

1. Unify release/build versions; add signed Android release artifacts and a tested
   Obtainium import. This is the smallest useful complete update path.
2. Add desktop release checks and download-page fallback; this benefits all
   portable packages while installer work proceeds.
3. Spike signed macOS Sparkle integration including generated-module loading;
   add Windows installer/WinSparkle and AppImage update metadata/verification.
4. Publish a Flatpak app remote, then consider portable self-installation only if
   demand justifies maintaining a custom helper.

Before enabling installation, test N to N+1 on each shipped architecture and
format; cancellation, offline/rate-limit responses, corrupt/wrong-platform and
unsigned assets, read-only locations, failed replacement, rollback, skipped
versions, and stable/preview separation. Confirm saves/settings survive and
an ABI-changing update rebuilds successfully from the player's ISO. For Android,
test both a fresh Obtainium-managed install and existing debug-install migration.
No runtime tests were run for this documentation-only investigation.
