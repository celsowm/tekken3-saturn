#!/bin/sh
# Per-function SH-2 costs of a Tekken Force probe build under Ymir's debug profile.
#   scripts/force_ymir_profile.sh BUILD OUT [FROM_FIELD] [FIELDS]
# Injects BUILD/tekken3_force.app.bin with BUILD/disc/tekken3_force.iso, traces FIELDS fields from FROM_FIELD and
# prints the most expensive master functions (scripts/saturn_pc_profile.py). OUT must not exist.
set -eu
build=$1 out=$2 from=${3:-1200} fields=${4:-240}
PATH=$HOME/dev/saturn-tools/bin:$PATH
root=$(cd "$(dirname "$0")/.." && pwd)
[ ! -e "$out" ] || { echo "$out exists" >&2; exit 1; }
mkdir -p "$out"
cp "$build/tekken3_force.elf" "$out/profile.elf"
# The probe's exit status is not a verdict here; the profile files are checked below.
"$HOME/dev/lib-saturn/harness/build-macos/probe" --iso "$build/disc/tekken3_force.iso" \
    --bin "$build/tekken3_force.app.bin" \
    --bios "$root/assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin" \
    --boot-frames 90 --frames $((from + fields + 30)) --sh2-cache on --port-device 1:pad \
    --debug-profile "$out/debug" --trace-from "$from" --trace-frames "$fields" \
    --screenshot "$((from - 1)):$out/screen.png" --out "$out/ymir.json" > "$out/log" 2>&1 || true
[ -s "$out/debug.pc.csv" ] || { echo "no profile; see $out/log" >&2; exit 1; }
python3 "$root/scripts/saturn_pc_profile.py" functions "$out/debug.pc.csv" "$out/profile.elf" | head -${TOP:-30}
