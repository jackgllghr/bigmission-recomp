#!/usr/bin/env bash
# Builds the launcher's Flatpak and bundles it as a single .flatpak file:
#   build_flatpak.sh <version> <out directory>
# Needs flatpak and flatpak-builder, and the Flathub remote for the
# Freedesktop SDK (added for the user if missing). The bundle names Flathub
# as where the SDK comes from, so `flatpak install <bundle>` fetches it.
set -euo pipefail

version=${1:?usage: build_flatpak.sh <version> <out directory>}
out=${2:?usage: build_flatpak.sh <version> <out directory>}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
app=io.github.sarusaru_recomp.SaruSaruRecomp
work="$root/out/flatpak"
case "$(uname -m)" in
    x86_64) host=linux-x64 ;;
    aarch64 | arm64) host=linux-arm64 ;;
    *) echo "build_flatpak.sh: unsupported machine $(uname -m)" >&2; exit 1 ;;
esac

# The source, as git sees it (tracked files and new ones it does not ignore,
# with their working-tree contents): never the git-ignored game data.
mkdir -p "$work" "$out"
staging="$work/manifest"
rm -rf "$staging"
mkdir -p "$staging"
cp "$here/$app.yml" "$staging/"
# The tag identifies the launcher without changing the game-module ABI.
sed -i "s/-DSARUSARU_GAME_MODULE=OFF /-DSARUSARU_RELEASE_VERSION=${version#v} -DSARUSARU_GAME_MODULE=OFF /" "$staging/$app.yml"
(cd "$root" && git ls-files -z --cached --others --exclude-standard |
    while IFS= read -r -d '' file; do [ -e "$file" ] && printf '%s\0' "$file"; done |
    tar --null -T - --transform 's|^|sarusaru-recomp/|' -czf "$staging/source.tar.gz")

flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak-builder --user --install-deps-from=flathub --force-clean --disable-rofiles-fuse \
    --repo="$work/repo" --state-dir="$work/state" "$work/build" "$staging/$app.yml"
flatpak build-bundle --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo \
    "$work/repo" "$out/SaruSaruRecomp-$version-$host.flatpak" "$app"
echo "$out/SaruSaruRecomp-$version-$host.flatpak"
