#!/usr/bin/env python3
"""Configure and build a private Paul–Law disc from prepared local assets."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from check_demo_sdk import check as check_sdk

ROOT = Path(__file__).resolve().parents[1]
ENABLED = '''ASSET_TOPOLOGY BRANCH_INPUT_FILTER CLOSE_CAMERA COLD_CAMERA_STREAMS
COLD_LOW_WRAM COMPACT_CAMERA_STREAMS DIRECT_FACE_SPANS DMA_COMMAND_LIST
ENABLE_PSX_COMBAT ENABLE_PSX_OWNED_MATCH ENABLE_PSX_SKIN_PREVIEW
FRAME_ON_SUBMIT FUSED_ACTOR_PREPARATION HIDE_FRAME_COUNTER HIGH_MATERIAL_PROGRAMS
HIGH_TEXTURE_DESCRIPTORS MASTER_WORKER_FACES OVERLAP_VDP1 PARALLEL_GEOMETRY
PARALLEL_SKINNING PARTICLES PROJECTED_FACE_QUEUE PSX_CAMERA SKIN_TO_SCREEN
SLAVE_SIMULATION TITLE_SCREEN TRUSTED_SCENE_FLUSH VDP2_FLOOR ORIGINAL_HUD ORIGINAL_HUD_BARS'''.split()
INPUTS = {
    'LOCAL_KMD': 'raw/071.bin', 'LOCAL_CHARACTER_ARC': 'raw/073.bin',
    'LOCAL_ACTOR0_KMD': 'raw/075.bin', 'LOCAL_ACTOR1_MESH_KMD': 'raw/079.bin',
    'LOCAL_ACTOR1_ARC': 'raw/081.bin', 'LOCAL_HUD_ACTOR0_ARC': 'raw/073.bin',
    'LOCAL_HUD_ACTOR1_ARC': 'raw/081.bin', 'LOCAL_HUD_EXE': 'SLUS_004.02',
    'LOCAL_CAMERA_BANK': 'raw/074.bin', 'LOCAL_SECOND_CAMERA_BANK': 'raw/082.bin',
    'LOCAL_CAMERA_EXE': 'SLUS_004.02',
    'LOCAL_DIVMOT': 'divmot/074/bank_0.t3logic',
    'LOCAL_SECOND_DIVMOT': 'divmot/082/bank_1.t3logic',
    'LOCAL_COMMON_DIVMOT': 'divmot/279/bank_99.t3logic',
    'LOCAL_PSX_SEQUENCES': 'sequences.json',
    'LOCAL_PSX_AI_TABLES': 'pose-tables/psx_ai.bin',
    'LOCAL_PSX_BODY_TABLES': 'pose-tables/psx_body.bin',
    'LOCAL_PSX_COMMON_BRANCHES': 'pose-tables/psx_common_branches.bin',
    'LOCAL_PSX_GROUND_TABLES': 'pose-tables/psx_ground.bin',
    'LOCAL_PSX_IK_TABLES': 'pose-tables/psx_ik.bin',
    'LOCAL_PSX_REACTIONS': 'pose-tables/psx_reactions.bin',
    'LOCAL_PSX_RSQRT_TABLE': 'pose-tables/psx_rsqrt.bin',
    'LOCAL_PSX_SPLINE_TABLES': 'pose-tables/psx_spline.bin',
    'LOCAL_CAMERA_SPLINE': 'pose-tables/psx_spline.bin',
    'LOCAL_PSX_TRIG_TABLE': 'pose-tables/psx_trig.bin',
}


def configure_command(assets, build, prefix, libsaturn, music, boot_payloads=None):
    assets, build, prefix, libsaturn = (p.resolve() for p in (assets, build, prefix, libsaturn))
    private_root = (ROOT / 'assets/imported').resolve()
    if private_root not in assets.parents:
        raise ValueError('prepared assets must be under private assets/imported')
    missing = [str(assets / rel) for rel in INPUTS.values() if not (assets / rel).is_file()]
    missing += [str(assets / rel) for rel in ('raw/036.bin', 'raw/056.bin', 'raw/072.bin',
                                              'raw/080.bin', 'pose-tables/psx_sounds.bin',
                                              'pose-tables/psx_effects.bin') if not (assets / rel).is_file()]
    wav = assets / 'stage-music/stage.wav'
    if music and not wav.is_file():missing.append(str(wav))
    toolchain = prefix / 'share/libsaturn/toolchains/sh2eb-elf.cmake'
    if not toolchain.is_file():missing.append(str(toolchain))
    if not (libsaturn / 'include/saturn/core.h').is_file():missing.append(str(libsaturn / 'include/saturn/core.h'))
    if missing:raise ValueError('missing build inputs: ' + ', '.join(missing))
    check_sdk(prefix)
    command = ['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Ninja',
               '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain),
               '-DCMAKE_PREFIX_PATH=' + str(prefix), '-DLIBSATURN_SOURCE_DIR=' + str(libsaturn)]
    command += ['-DT3_' + name + '=ON' for name in ENABLED]
    command += ['-DT3_' + name + '=' + str(assets / rel) for name, rel in INPUTS.items()]
    command += ['-DT3_LOCAL_PAUL_STAGE=' + str(assets), '-DT3_LOCAL_SFX=' + str(assets),
                '-DT3_LOCAL_ACTOR1_KMD=', '-DT3_SHARE_ACTOR_RESOURCES=OFF',
                '-DT3_WORLD_POSE_CACHE_ENTRIES=0', '-DT3_SECOND_CHARACTER_ID=1',
                '-DT3_VIEWPORT_DX=6', '-DT3_VIEWPORT_SIGN=2', '-DT3_STAGE_CDDA=' + ('ON' if music else 'OFF')]
    if (assets / 'raw/010.bin').is_file():
        command.append('-DT3_LOCAL_TITLE_OVERLAY=' + str(assets / 'raw/010.bin'))
    if music:command.append('-DT3_STAGE_CDDA_WAV=' + str(wav))
    if boot_payloads is not None:
        boot_payloads = boot_payloads.resolve()
        if not boot_payloads.is_file():raise ValueError('boot payload manifest does not exist: ' + str(boot_payloads))
        command.append('-DT3_BOOT_PAYLOADS=' + str(boot_payloads))
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True, help='output of prepare_paul_law_assets.py')
    parser.add_argument('--build', type=Path, required=True, help='fresh build directory')
    parser.add_argument('--prefix', type=Path, required=True, help='installed LibSaturn prefix')
    parser.add_argument('--libsaturn', type=Path, required=True, help='LibSaturn source checkout')
    parser.add_argument('--music', action='store_true', help='build a mixed-mode disc with the extracted stage theme')
    parser.add_argument('--boot-payloads', type=Path, help='optional LibSaturn native boot payload manifest')
    parser.add_argument('--configure-only', action='store_true')
    args = parser.parse_args()
    try:
        if args.build.exists() and any(args.build.iterdir()):
            raise ValueError('use a fresh empty build directory')
        command = configure_command(args.assets, args.build, args.prefix, args.libsaturn, args.music, args.boot_payloads)
        subprocess.run(command, cwd=ROOT, check=True)
        if not args.configure_only:
            subprocess.run(['cmake', '--build', str(args.build.resolve()), '--parallel', '4'], cwd=ROOT, check=True)
        print(json.dumps(dict(build=str(args.build.resolve()), music=args.music,
                              configured=True, built=not args.configure_only,
                              scope='Private local build; generated disc contains retail-derived data.'), indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
