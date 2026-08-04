#!/usr/bin/env bash
# test_appimage_layout.sh — verify a built Tomba2Recomp AppImage seeds the
# writable data directory correctly, without launching the game.
#
# Runs the AppImage with TOMBA2_RECOMP_SEED_ONLY=1, which makes AppRun perform
# its seeding and print the data directory instead of exec'ing the runtime.
# Then asserts the layout the release promises.
#
# Usage: bash tools/test_appimage_layout.sh path/to/Tomba2Recomp-<ver>-linux-x86_64.AppImage
set -euo pipefail

appimage=${1:-}
[ -n "$appimage" ] || { echo "usage: $0 <AppImage>" >&2; exit 2; }
[ -x "$appimage" ] || { echo "not executable: $appimage" >&2; exit 1; }

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
expected_version=$(tr -d ' \t\r\n' < "$root/packaging/release/VERSION")

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

data_dir=$(TOMBA2_RECOMP_DATA_DIR="$work/data" TOMBA2_RECOMP_SEED_ONLY=1 \
    "$appimage" --appimage-extract-and-run)
[ -n "$data_dir" ] || { echo "AppRun printed no data dir" >&2; exit 1; }

fail=0
check_file() { [ -f "$data_dir/$1" ] || { echo "MISSING file: $1" >&2; fail=1; }; }
check_dir()  { [ -d "$data_dir/$1" ] || { echo "MISSING dir:  $1" >&2; fail=1; }; }

for d in saves cache mods assets bios; do check_dir "$d"; done
for f in game.toml input.ini START_HERE.txt LICENSE README.md \
         bios/openbios.bin bios/OpenBIOS.LICENSE .appimage-layout-version; do
    check_file "$f"
done

got_version=$(tr -d ' \t\r\n' < "$data_dir/.appimage-layout-version")
if [ "$got_version" != "$expected_version" ]; then
    echo "version marker mismatch: AppImage says '$got_version', VERSION says '$expected_version'" >&2
    fail=1
fi

manifests=$(find "$data_dir/mods" -name manifest.toml | wc -l)
if [ "$manifests" -ne 3 ]; then
    echo "expected 3 mod manifests in the seeded catalog, found $manifests" >&2
    fail=1
fi

# A retail BIOS or disc image must never be inside the payload.
stray=$(find "$data_dir" \( -iname 'SCPH*.BIN' -o -iname '*.cue' -o -iname '*.iso' \
        -o -iname '*.mcd' \) -print 2>/dev/null || true)
if [ -n "$stray" ]; then
    echo "payload contains files that must never ship:" >&2
    printf '  %s\n' $stray >&2
    fail=1
fi

# Seeding must be idempotent: a second run must not fail or duplicate.
TOMBA2_RECOMP_DATA_DIR="$work/data" TOMBA2_RECOMP_SEED_ONLY=1 \
    "$appimage" --appimage-extract-and-run >/dev/null

# User-owned files must survive a reseed.
echo "; user edit" >> "$data_dir/input.ini"
before=$(sha256sum "$data_dir/input.ini" | awk '{print $1}')
TOMBA2_RECOMP_DATA_DIR="$work/data" TOMBA2_RECOMP_SEED_ONLY=1 \
    "$appimage" --appimage-extract-and-run >/dev/null
after=$(sha256sum "$data_dir/input.ini" | awk '{print $1}')
if [ "$before" != "$after" ]; then
    echo "reseed clobbered user-owned input.ini" >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "AppImage layout test FAILED" >&2
    exit 1
fi
echo "AppImage layout test passed ($expected_version)"
