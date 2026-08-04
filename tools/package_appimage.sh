#!/usr/bin/env bash
# package_appimage.sh — build the Linux x86_64 AppImage release for Tomba2Recomp.
#
# Counterpart to tools/package_release.ps1 (Windows). Both packagers read the
# SAME sources so the two platforms cannot drift:
#
#   packaging/release/VERSION      the version string (single source of truth)
#   packaging/release/game.toml    the player-facing config
#   packaging/release/input.ini    the default controller mapping
#   packaging/release/START_HERE.txt
#
# Reproducibility:
#   * the version is never hardcoded here or in AppRun (AppRun's marker is
#     stamped from VERSION at package time),
#   * linuxdeploy/appimagetool are pinned by URL + SHA256 and verified,
#   * SOURCE_DATE_EPOCH is derived from the git commit (override to pin it),
#     and every staged file's mtime is normalised to it before squashing,
#   * the run prints SHA256 for the artifact.
#
# WSL: building an AppDir directly on a /mnt/<drive> DrvFs mount fails or
# silently degrades — symlinks need metadata mount options and the exec bit is
# not preserved. When the repo lives on /mnt we therefore stage the AppDir on
# the native Linux filesystem and copy only the finished .AppImage back to the
# Windows-visible output path. Pass --out to place it elsewhere; a Windows-style
# path (F:\... or F:/...) is translated with wslpath.
#
# Usage:
#   bash tools/package_appimage.sh                     # version from VERSION
#   bash tools/package_appimage.sh --version v0.0.7
#   bash tools/package_appimage.sh --out /mnt/f/drop   # or --out 'F:\drop'
#   bash tools/package_appimage.sh --skip-build        # reuse existing build dir
#
# Prereqs: cmake, ninja or make, a C/C++ toolchain, libsdl2-dev,
# libgl1-mesa-dev, curl, ImageMagick (for the icon), and a generated/ tree
# (the recompiler runs on Windows; generated C is committed).
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

version=""
out_dir=""
skip_build=0
build_dir=${BUILD_DIR:-"$root/build-appimage"}
jobs=${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

while [ $# -gt 0 ]; do
    case "$1" in
        --version) version=$2; shift 2;;
        --out)     out_dir=$2; shift 2;;
        --build-dir) build_dir=$2; shift 2;;
        --jobs)    jobs=$2; shift 2;;
        --skip-build) skip_build=1; shift;;
        -h|--help) sed -n '2,36p' "$0"; exit 0;;
        *) echo "unknown arg: $1" >&2; exit 2;;
    esac
done

[ -n "$version" ] || version=$(tr -d ' \t\r\n' < "$root/packaging/release/VERSION")
[ -n "$version" ] || { echo "empty version" >&2; exit 1; }

# --- path handling ---------------------------------------------------------
# Accept Windows-style output paths so the same command works from a WSL shell
# driven by a Windows tool.
to_unix_path() {
    case "$1" in
        [A-Za-z]:[/\\]*)
            if command -v wslpath >/dev/null 2>&1; then wslpath -u "$1"; else
                echo "cannot translate Windows path '$1' (wslpath missing)" >&2; exit 2
            fi;;
        *) printf '%s\n' "$1";;
    esac
}
[ -n "$out_dir" ] && out_dir=$(to_unix_path "$out_dir")
out_dir=${out_dir:-"$root/release-linux"}
mkdir -p -- "$out_dir"
out_dir=$(CDPATH= cd -- "$out_dir" && pwd)

is_wsl=0
if [ -r /proc/version ] && grep -qiE 'microsoft|wsl' /proc/version; then is_wsl=1; fi

# DrvFs (/mnt/<drive>) cannot host the AppDir: linuxdeploy needs real symlinks
# and preserved exec bits. Stage on the native filesystem and copy the finished
# artifact back.
stage_base=$build_dir
case "$root" in
    /mnt/*)
        if [ "$is_wsl" = "1" ]; then
            stage_base=${TMPDIR:-/tmp}/tomba2recomp-appimage.$$
            echo "WSL: repo is on a DrvFs mount; staging AppDir at $stage_base"
        fi;;
esac
appdir=$stage_base/AppDir
tools_dir=${RECOMP_APPIMAGE_TOOLS:-"${XDG_CACHE_HOME:-$HOME/.cache}/recomp-appimage-tools"}
output=$out_dir/Tomba2Recomp-$version-linux-x86_64.AppImage

cleanup() {
    case "$stage_base" in
        /tmp/tomba2recomp-appimage.*|"${TMPDIR:-/tmp}"/tomba2recomp-appimage.*)
            rm -rf -- "$stage_base";;
    esac
}
trap cleanup EXIT

if [ ! -f "$root/generated/SCUS_944.54_dispatch.c" ]; then
    echo "Missing generated game sources (generated/SCUS_944.54_dispatch.c)." >&2
    echo "Run the recompiler first: psxrecomp-v4/recompiler/build*/psxrecomp-game --config game.toml" >&2
    exit 1
fi

# --- reproducibility anchor ------------------------------------------------
if [ -z "${SOURCE_DATE_EPOCH:-}" ]; then
    SOURCE_DATE_EPOCH=$(git -C "$root" log -1 --format=%ct 2>/dev/null || echo 0)
fi
export SOURCE_DATE_EPOCH
echo "version=$version  SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH"

# --- build -----------------------------------------------------------------
if [ "$skip_build" = "0" ]; then
    generator=Ninja
    command -v ninja >/dev/null 2>&1 || generator="Unix Makefiles"
    cmake -S "$root" -B "$build_dir" -G "$generator" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPSX_DEBUG_TOOLS=OFF
    cmake --build "$build_dir" --target psx-runtime -j "$jobs"
fi

elf=$build_dir/Tomba2Recomp
[ -f "$elf" ] || elf=$build_dir/psx-runtime
[ -f "$elf" ] || { echo "no runtime ELF under $build_dir" >&2; exit 1; }
file -b "$elf" | grep -q ELF || { echo "$elf is not an ELF binary" >&2; exit 1; }

# --- stage AppDir ----------------------------------------------------------
rm -rf -- "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/tomba2recomp"
payload=$appdir/usr/share/tomba2recomp

install -m 0755 "$elf" "$appdir/usr/bin/Tomba2Recomp"

# AppRun carries the version marker; stamp it rather than hardcoding.
sed "s|@VERSION@|$version|g" "$root/packaging/linux/AppRun" > "$appdir/AppRun"
chmod 0755 "$appdir/AppRun"
install -m 0644 "$root/packaging/linux/io.github.mstan.Tomba2Recomp.desktop" \
    "$appdir/io.github.mstan.Tomba2Recomp.desktop"

for tree in assets bios mods; do
    [ -d "$build_dir/$tree" ] || { echo "build did not stage $tree/" >&2; exit 1; }
    cp -a "$build_dir/$tree" "$payload/$tree"
done

# The Mods page must never ship empty: assert the three preloaded packages.
mod_manifests=$(find "$payload/mods" -name manifest.toml | wc -l)
if [ "$mod_manifests" -ne 3 ]; then
    echo "expected 3 preloaded mod manifests, found $mod_manifests" >&2
    exit 1
fi

# OpenBIOS must ride along with its notice; a retail BIOS must not.
[ -f "$payload/bios/openbios.bin" ] || { echo "missing bundled OpenBIOS" >&2; exit 1; }
[ -f "$payload/bios/OpenBIOS.LICENSE" ] || { echo "missing OpenBIOS notice" >&2; exit 1; }

mkdir -p "$payload/licenses"
if [ -f "$root/psxrecomp-v4/runtime/licenses/libchdr-NOTICES.txt" ]; then
    cp "$root/psxrecomp-v4/runtime/licenses/libchdr-NOTICES.txt" "$payload/licenses/"
fi

cp "$root/packaging/release/game.toml"      "$payload/game.toml"
cp "$root/packaging/release/input.ini"      "$payload/input.ini"
cp "$root/packaging/release/START_HERE.txt" "$payload/START_HERE.txt"
cp "$root/LICENSE" "$root/README.md" "$payload/"

# recomp-ui resolves fonts/textures through SDL_GetBasePath(), which points at
# the real ELF inside the mount rather than psxrecomp's writable argv[0] anchor.
ln -s ../share/tomba2recomp/assets "$appdir/usr/bin/assets"

if command -v magick >/dev/null 2>&1; then image_tool=magick
elif command -v convert >/dev/null 2>&1; then image_tool=convert
else echo "ImageMagick is required for the AppImage icon." >&2; exit 1; fi
"$image_tool" "$root/recomp/launcher/boxart.tga" \
    -resize 240x240 -background transparent -gravity center -extent 256x256 \
    "$appdir/io.github.mstan.Tomba2Recomp.png"
ln -s io.github.mstan.Tomba2Recomp.png "$appdir/.DirIcon"

# --- pinned tooling --------------------------------------------------------
linuxdeploy_url=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
linuxdeploy_sha=421ca71d5c69ea97c6309276232990d43df1dcece0edfaa26bbf926ff96ed12e
appimagetool_url=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
appimagetool_sha=a6d71e2b6cd66f8e8d16c37ad164658985e0cf5fcaa950c90a482890cb9d13e0

mkdir -p "$tools_dir"
fetch_tool() {
    url=$1; sha=$2; dest=$3
    if [ ! -f "$dest" ] || [ "$(sha256sum "$dest" | awk '{print $1}')" != "$sha" ]; then
        curl -fL --retry 3 "$url" -o "$dest.tmp"
        printf '%s  %s\n' "$sha" "$dest.tmp" | sha256sum -c -
        mv "$dest.tmp" "$dest"
    fi
    chmod 0755 "$dest"
}
linuxdeploy=$tools_dir/linuxdeploy-x86_64.AppImage
appimagetool=$tools_dir/appimagetool-x86_64.AppImage
fetch_tool "$linuxdeploy_url" "$linuxdeploy_sha" "$linuxdeploy"
fetch_tool "$appimagetool_url" "$appimagetool_sha" "$appimagetool"

export NO_STRIP=1
"$linuxdeploy" --appimage-extract-and-run \
    --appdir "$appdir" \
    --executable "$appdir/usr/bin/Tomba2Recomp" \
    --desktop-file "$appdir/io.github.mstan.Tomba2Recomp.desktop" \
    --icon-file "$appdir/io.github.mstan.Tomba2Recomp.png"

# Normalise mtimes so the squashfs image is byte-stable across runs.
find "$appdir" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} + 2>/dev/null || true

rm -f -- "$output"
ARCH=x86_64 "$appimagetool" --appimage-extract-and-run "$appdir" "$output"
chmod 0755 "$output"

sha256sum "$output"
echo "AppImage: $output"
