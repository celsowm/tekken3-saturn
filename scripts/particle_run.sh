#!/bin/sh
# Run a build for N fields and decode the particle/projection counters.
# usage: particle_run.sh BUILD OUT FIELDS [extra saturn_probe args]
set -e
b=$1;o=$2;n=$3;shift 3
args=
nm="${NM:-/Users/celso/dev/saturn-tools/bin/sh2eb-elf-nm}"
for s in g_t3_ordered_mask_images g_t3_ordered_mask_fallbacks g_t3_mixed_ot_frames g_t3_mixed_ot_empty_frames g_t3_original_projected g_t3_original_projected_fallbacks g_t3_particle_images g_t3_particle_max_sprites g_t3_particle_prepare_images g_t3_ordered_mask_tiles g_t3_cell_check_faces g_t3_cell_check_mismatch g_t3_cell_check_visited g_t3_ordered_mask_cell_drops g_t3_ordered_mask_face_failures g_t3_ordered_mask_particle_failures; do "$nm" "$b/tekken3_saturn.elf" | grep -q " [BbDd] _$s\$" && args="$args --dump-symbol $s"; done
python3 scripts/saturn_probe.py --build "$b" --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin --frames "$n" $args "$@" --out "$o" > "$o.log" 2>&1 || { tail -5 "$o.log"; exit 1; }
python3 - "$o" <<'PY'
import json,struct,sys,re
d=sys.argv[1]+"/"
j=json.load(open(d+"symbol-dumps.json"))
for k,e in j.items():
    if any(w in k for w in("ordered","mixed","original_proj","_particle_","cell_check","cell_drops","_failures")):
        raw=open(d+e["file"],"rb").read()
        print(k,[struct.unpack('>I',raw[i:i+4])[0] for i in range(0,len(raw)-3,4)])
t=open(sys.argv[1]+".log").read()
for key in ("sim_ticks","loop_rate_x10","frame_work_ms","submitted_frames","discarded_fields","first_error"):
    m=re.search(r'"%s": (\d+)'%key,t)
    print(key,m.group(1) if m else None)
PY
