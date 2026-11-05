#!/usr/bin/env python3
"""Cut the opening movie from the USA disc and pack it as INTRO.MOV for the Saturn.

The PS1 movie is an STR/MDEC stream. ffmpeg decodes it (-f psxstr); LibSaturn's tools/satmovie.py scales it,
encodes it as Cinepak and IMA ADPCM and writes the movie file `sat_movie_play` reads. What this script adds is
finding the movie on the disc and lining its XA sound up with its first frame.

Commands:
  probe   encode a window at each candidate picture size and print bytes per frame
  build   write the movie file
Private output: build/intro-* (git-ignored). The movie is game data and never goes into git.
"""
import argparse
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import psx_movie  # noqa: E402

OPENING_FRAMES, OPENING_SIZE = 1950, (256, 224)
FRAME_US = 50000                        # 14625 video sectors at 150 per second over 1950 frames
SIZES = {'256x180': (256, 180), '240x168': (240, 168), '224x156': (224, 156)}
DEFAULT_SIZE = '240x168'
WORK = ROOT / 'build/intro-probe'
SECTORS_PER_SECOND = 150


def satmovie_module(libsaturn=None):
    """LibSaturn's converter: from --libsaturn, the installed SDK, the sibling or CI checkout."""
    candidates = [Path(libsaturn) / 'tools'] if libsaturn else []
    if os.environ.get('LIBSATURN_PREFIX'):
        candidates.append(Path(os.environ['LIBSATURN_PREFIX']) / 'share/libsaturn/tools')
    candidates += [ROOT.parent / 'lib-saturn/tools', ROOT.parent / 'saturn-prefix/share/libsaturn/tools',
                   ROOT / '.external/libsaturn/tools']
    for folder in candidates:
        if (folder / 'satmovie.py').exists():
            sys.path.insert(0, str(folder))
            import satmovie
            return satmovie
    raise SystemExit('satmovie.py not found: pass --libsaturn or set LIBSATURN_PREFIX')


def extract_opening(track1, out):
    handle, image, lba, movies = psx_movie.open_movies(track1)
    try:
        found = [m for m in movies if m.frames == OPENING_FRAMES and (m.width, m.height) == OPENING_SIZE]
        if len(found) != 1:
            raise SystemExit(f'expected one {OPENING_FRAMES}-frame {OPENING_SIZE} movie, found {len(found)}')
        sectors = max(m.last for m in movies) + 1
        out.write_bytes(psx_movie.cut(image, lba, found[0], sectors, movies))
        return found[0]
    finally:
        image.close()
        handle.close()


def audio_start_seconds(raw):
    """When the movie's audio begins, in seconds after its first video sector (negative: before it).

    XA audio sectors are Form 2 (submode bit 5); video sectors are Form 1 STR data."""
    video = audio = None
    for i in range(len(raw) // psx_movie.RAW):
        base = i * psx_movie.RAW
        if raw[base + 18] & 0x20:
            audio = i if audio is None else audio
        elif struct.unpack_from('<HH', raw, base + psx_movie.USER) == (psx_movie.STR_MAGIC, psx_movie.STR_TYPE):
            video = i if video is None else video
        if video is not None and audio is not None:
            break
    return (audio - video) / SECTORS_PER_SECOND


def source(args):
    WORK.mkdir(parents=True, exist_ok=True)
    stream = WORK / 'opening.str'
    if not stream.exists():
        movie = extract_opening(args.track1, stream)
        print(f'opening: sectors {movie.first}..{movie.last}, {movie.frames} frames, {stream.stat().st_size >> 20} MiB raw')
    return stream


def probe(args):
    sat = satmovie_module(args.libsaturn)
    stream = source(args)
    window = ['-t', args.seconds] if args.seconds else []
    for name, size in SIZES.items():
        out = WORK / f'op-{name}.avi'
        sat.encode_cinepak(['-f', 'psxstr', *window, '-i', stream], size, args.quality, out)
        frames = len(sat.split_frames(out, WORK))
        print(f'{name}: {out.stat().st_size / frames / 1024:5.1f} KiB/frame over {frames} frames')
        out.unlink()


def build(args):
    sat = satmovie_module(args.libsaturn)
    stream = source(args)
    size = SIZES[args.size]
    avi = WORK / f'op-{args.size}-q{args.quality}.avi'
    if args.fresh or not avi.exists():
        sat.encode_cinepak(['-f', 'psxstr', '-i', stream], size, args.quality, avi)
    frames = sat.split_frames(avi, WORK)
    for frame in frames:
        got = sat.check_frame(frame)
        if got != size:
            raise SystemExit(f'frame is {got}, expected {size}')
    if len(frames) != OPENING_FRAMES:
        raise SystemExit(f'{len(frames)} frames, expected {OPENING_FRAMES}')
    start = audio_start_seconds(stream.read_bytes())
    pcm = sat.decode_audio(['-f', 'psxstr', '-i', stream, '-map', '0:a:0'], WORK / 'opening.pcm')
    if start >= 0:
        pcm = b'\0' * (round(start * sat.AUDIO_RATE) * 4) + pcm
    else:
        pcm = pcm[round(-start * sat.AUDIO_RATE) * 4:]
    sat.write_movie(args.out, size, frames, sat.spread_audio(pcm, len(frames), FRAME_US), FRAME_US)
    print(f'{args.out}: {len(frames)} frames, {args.out.stat().st_size / 1048576:.1f} MiB; '
          f'audio starts {start * 1000:.0f} ms after the first frame')


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--track1', type=Path, default=psx_movie.TRACK1)
    p.add_argument('--libsaturn', type=Path, help='a LibSaturn checkout or install holding tools/satmovie.py')
    sub = p.add_subparsers(dest='command', required=True)
    q = sub.add_parser('probe', help='encode a window at each picture size and print bytes per frame')
    q.add_argument('--seconds', type=int, default=20)
    q.add_argument('--quality', type=int, default=20)
    q.set_defaults(run=probe)
    b = sub.add_parser('build', help='write the movie file')
    b.add_argument('--size', choices=sorted(SIZES), default=DEFAULT_SIZE)
    b.add_argument('--quality', type=int, default=20)
    b.add_argument('--out', type=Path, default=WORK / 'INTRO.MOV')
    b.add_argument('--fresh', action='store_true', help='encode again even if the encoder output exists')
    b.set_defaults(run=build)
    args = p.parse_args()
    args.run(args)


if __name__ == '__main__':
    main()
