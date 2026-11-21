#!/usr/bin/env bash
# Native macOS SAROO movie A/B qualification (private discs stay on this Mac).
# Run from any directory: bash scripts/qualify-saroo-movie-mac.sh
# Overrides: T3_BUILD_DIR, LIBSATURN_SOURCE_DIR, LIBSATURN_PREFIX, LIBSATURN_BUILD_DIR.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
die() { printf '[saroo-movie] ERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '\n[saroo-movie] %s\n' "$*"; }
cache_value() {
    local key="$1" file="$2"
    sed -n "s/^${key}:[^=]*=//p" "$file" | head -1
}

command -v git >/dev/null || die "git missing"
command -v cmake >/dev/null || die "cmake missing"
command -v python3 >/dev/null || die "python3 missing"

say "Pulling game main (no discarded local work)"
git -C "$ROOT" pull --ff-only origin main

# Prefer the existing private CMake build with the most recently modified cache;
# it already knows the paths to the user's retail-derived assets.
if [[ -n "${T3_BUILD_DIR:-}" ]]; then
    BUILD="$(cd "$T3_BUILD_DIR" && pwd)"
else
    BUILD="$(python3 - "$ROOT" <<'PY'
import sys
from pathlib import Path
root = Path(sys.argv[1])
caches = [p for p in (root / 'build').rglob('CMakeCache.txt') if
          ('CMAKE_SYSTEM_NAME:INTERNAL=Generic' in p.read_text(errors='replace') or
           'T3_LOCAL_SFX:PATH=' in p.read_text(errors='replace')) and
          'LIBSATURN_SOURCE_DIR:' in p.read_text(errors='replace')]
caches.sort(key=lambda p: p.stat().st_mtime, reverse=True)
if caches:
    print(caches[0].parent)
PY
)"
fi
[[ -n "$BUILD" && -f "$BUILD/CMakeCache.txt" ]] ||
    die "No existing Saturn CMake build found. Set T3_BUILD_DIR=/absolute/path/to/your/private/build"
CACHE="$BUILD/CMakeCache.txt"
say "Using existing game build: $BUILD"

SDK="${LIBSATURN_SOURCE_DIR:-$(cache_value LIBSATURN_SOURCE_DIR "$CACHE")}"
if [[ -z "$SDK" || ! -f "$SDK/include/saturn/movie.h" ]]; then
    for guess in "$ROOT/../lib-saturn" "$ROOT/../libsaturn" "$HOME/dev/lib-saturn" "$HOME/dev/libsaturn"; do
        if [[ -f "$guess/include/saturn/movie.h" ]]; then SDK="$guess"; break; fi
    done
fi
[[ -f "$SDK/CMakeLists.txt" ]] || die "LibSaturn source not found. Set LIBSATURN_SOURCE_DIR=/your/libsaturn"
SDK="$(cd "$SDK" && pwd)"

PREFIX="${LIBSATURN_PREFIX:-$(cache_value CMAKE_PREFIX_PATH "$CACHE")}"
PREFIX="${PREFIX%%;*}"
[[ -n "$PREFIX" ]] || die "Installed SDK prefix missing. Set LIBSATURN_PREFIX=/your/saturn-prefix"
mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd)"
[[ -f "$SDK/cmake/toolchains/sh2eb-elf.cmake" ]] || die "Missing LibSaturn SH-2 toolchain"

say "Pulling LibSaturn main"
git -C "$SDK" pull --ff-only origin main
say "Checking the offline movie-signature regression"
python3 "$ROOT/tests/test_movie_signature.py"
say "Game: $(git -C "$ROOT" rev-parse --short HEAD); SDK: $(git -C "$SDK" rev-parse --short HEAD)"

SDK_BUILD="${LIBSATURN_BUILD_DIR:-$SDK/build/saturn-release}"
if [[ ! -f "$SDK_BUILD/CMakeCache.txt" ]]; then
    SDK_BUILD="$SDK/build/saroo-movie-diagnostics"
fi
say "Building and installing matching LibSaturn runtime: $PREFIX"
if [[ -f "$SDK_BUILD/CMakeCache.txt" ]]; then
    cmake -S "$SDK" -B "$SDK_BUILD" \
        -DCMAKE_TOOLCHAIN_FILE="$SDK/cmake/toolchains/sh2eb-elf.cmake" \
        -DCMAKE_BUILD_TYPE=Release
else
    GENERATOR=()
    if command -v ninja >/dev/null; then GENERATOR=(-G Ninja); fi
    cmake -S "$SDK" -B "$SDK_BUILD" "${GENERATOR[@]}" \
        -DCMAKE_TOOLCHAIN_FILE="$SDK/cmake/toolchains/sh2eb-elf.cmake" \
        -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$SDK_BUILD" --parallel 4
cmake --install "$SDK_BUILD" --prefix "$PREFIX"

say "Rebuilding user's existing private game configuration with the diagnostic screen ON"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DLIBSATURN_SOURCE_DIR="$SDK" -DT3_MOVIE_DIAGNOSTICS=ON -DT3_TEST_SKIP_INTRO=OFF
cmake --build "$BUILD" --parallel 4

OUT="$ROOT/build/saroo-movie-result/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
REPORT="$OUT/expected-hashes.txt"
{
    echo "Game main: $(git -C "$ROOT" rev-parse HEAD)"
    echo "LibSaturn main: $(git -C "$SDK" rev-parse HEAD)"
    echo "Build: $BUILD"
    echo "Expected signatures of ORIGINAL files. Compare PACKETS and HASH to each Saturn result screen."
} > "$REPORT"

MOVIES=()
for movie in "$BUILD/menu/LSINTRO.MOV" "$BUILD/menu/INTRO.MOV"; do
    [[ -f "$movie" ]] && MOVIES+=("$movie")
done
# INTRO.MOV can be staged directly from T3_LOCAL_INTRO_MOVIE without a menu copy.
LOCAL_INTRO="$(cache_value T3_LOCAL_INTRO_MOVIE "$CACHE")"
if [[ -f "$LOCAL_INTRO" && ! -f "$BUILD/menu/INTRO.MOV" ]]; then
    MOVIES+=("$LOCAL_INTRO")
fi
[[ ${#MOVIES[@]} -gt 0 ]] || die "No movie assets found; inspect the game's configured movie paths"
say "Computing reference signatures"
python3 "$ROOT/scripts/check_movie_signature.py" "${MOVIES[@]}" | tee -a "$REPORT"
for movie in "${MOVIES[@]}"; do shasum -a 256 "$movie" >> "$REPORT"; done

CUE="$BUILD/disc/music.cue"
[[ -f "$CUE" ]] || CUE="$BUILD/disc/tekken3_saturn.cue"
[[ -f "$CUE" ]] || die "Build succeeded but no expected CUE found under $BUILD/disc"
say "Copying private disc and CUE dependencies to local SAROO package"
python3 - "$CUE" "$OUT" <<'PY'
import re
import shutil
import sys
from pathlib import Path
cue, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
shutil.copy2(cue, out / cue.name)
files = re.findall(r'^\s*FILE\s+"([^"]+)"', cue.read_text(errors='replace'), flags=re.I | re.M)
if not files:
    raise SystemExit('CUE contains no quoted FILE entries; inspect manually: ' + str(cue))
for item in files:
    source = (cue.parent / item).resolve()
    if not source.is_file():
        raise SystemExit('Missing CUE dependency: ' + str(source))
    target = out / item
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
print('Local SAROO disc:', out / cue.name)
PY

cat > "$OUT/HOW_TO_TEST.txt" <<'EOF'
Boot the .cue from this directory in SAROO.
The first movie is the exact LibSaturn logo, LSINTRO.MOV.
When it finishes (or fails), photograph the movie diagnostics screen.
Write down PACKETS and HASH (decimal) and compare with expected-hashes.txt.
Press START once to advance to the next movie (INTRO.MOV when configured).
If the hashes match despite a corrupt picture/audio: investigate VDP2 DMA, SCSP,
the A/V connection and runtime state. If they do not match: investigate CD Block,
SAROO sector transfer, buffer overwrites and disc image generation.
Never publicly upload the packaged disc: it may contain retail game data.
EOF
say "Done. Private SAROO package and report: $OUT"
printf '[saroo-movie] Use this file on SAROO: %s\n' "$OUT/$(basename "$CUE")"
