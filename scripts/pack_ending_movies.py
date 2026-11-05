#!/usr/bin/env python3
"""Cut the arcade ending movies from the USA disc and pack them as ENDnn.MOV for the Saturn.

The original picks the winner's ending through the movie descriptors of ending.ovl
(third_party/Tekken3Remake docs/research/formats/sound-and-video.md, "Movie table"): character c
uses descriptor 3 + c, whose sector row is an STR movie of TEKKEN3.XAS, played to the descriptor's
last frame. On the USA disc rows 1-20 are the twenty ending STRs in disc order (psx_movie.py), so
character c plays STR c, except that 17 and 18 swap rows. Doctor B. and True Ogre reuse
Yoshimitsu's and Ogre's movies; INTRO.BIN maps them, so only END00-END18 are written. Each movie
goes through pack_intro_video.py's path: ffmpeg decodes it, LibSaturn's satmovie.py encodes Cinepak
and IMA ADPCM, and its XA sound is lined up with its first frame. The captions the original draws
over four endings are not reproduced.

    scripts/pack_ending_movies.py [--only 0 4 ...] [--quality 20] [--out build/ending-movies]

Private output (git-ignored): the movies are game data and never go into git. Configure the game
with -DT3_LOCAL_ENDING_MOVIES=<out> to put them on the disc.
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import psx_movie  # noqa: E402
from pack_intro_video import SECTORS_PER_SECOND, audio_start_seconds, satmovie_module  # noqa: E402

# Character -> (STR index on the USA disc, last frame of its descriptor), from the USA ending.ovl table.
ENDINGS = {0: (0, 640), 1: (1, 451), 2: (2, 816), 3: (3, 686), 4: (4, 522), 5: (5, 741), 6: (6, 724),
           7: (7, 673), 8: (8, 800), 9: (9, 817), 10: (10, 824), 11: (11, 388), 12: (12, 682),
           13: (13, 640), 14: (14, 716), 15: (15, 540), 16: (16, 818), 17: (18, 1002), 18: (17, 553)}
ENDING_MOVIES = 20
WIDTH, FULL_HEIGHT = 240, 168           # the opening's picture: 256x224 scaled to 240x168


def picture(movie):
    """The opening's scale for every source height, rounded to Cinepak's 4-pixel blocks."""
    return WIDTH, round(FULL_HEIGHT * movie.height / 224 / 4) * 4


def pack(sat, image, lba, movies, sectors, character, quality, out, work):
    index, last = ENDINGS[character]
    movie = movies[index]
    raw = psx_movie.cut(image, lba, movie, sectors, movies)
    stream = work / f'end{character:02d}.str'
    stream.write_bytes(raw)
    size = picture(movie)
    frame_us = round((movie.last - movie.first + 1) / SECTORS_PER_SECOND / movie.frames * 1e6)
    avi = work / f'end{character:02d}.avi'
    sat.encode_cinepak(['-f', 'psxstr', '-i', stream], size, quality, avi)
    frames = sat.split_frames(avi, work)[:last]
    for frame in frames:
        if sat.check_frame(frame) != size:
            raise SystemExit(f'ending {character}: a frame is {sat.check_frame(frame)}, expected {size}')
    start = audio_start_seconds(raw)
    pcm = sat.decode_audio(['-f', 'psxstr', '-i', stream, '-map', '0:a:0'], work / f'end{character:02d}.pcm')
    pcm = b'\0' * (round(start * sat.AUDIO_RATE) * 4) + pcm if start >= 0 else pcm[round(-start * sat.AUDIO_RATE) * 4:]
    pcm = pcm[:len(frames) * frame_us * sat.AUDIO_RATE // 1000000 * 4]
    target = out / f'END{character:02d}.MOV'
    sat.write_movie(target, size, frames, sat.spread_audio(pcm, len(frames), frame_us), frame_us)
    for scratch in (stream, avi, work / f'end{character:02d}.pcm'):
        scratch.unlink(missing_ok=True)
    print(f'{target.name}: STR {index}, {len(frames)} frames of {size[0]}x{size[1]} at {frame_us} us, '
          f'{target.stat().st_size / 1048576:.1f} MiB', flush=True)
    return target.stat().st_size


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--track1', type=Path, default=psx_movie.TRACK1)
    p.add_argument('--libsaturn', type=Path, help='a LibSaturn checkout or install holding tools/satmovie.py')
    p.add_argument('--quality', type=int, default=20)
    p.add_argument('--out', type=Path, default=ROOT / 'build/ending-movies')
    p.add_argument('--only', type=int, nargs='+', choices=sorted(ENDINGS), help='pack these characters only')
    args = p.parse_args()
    sat = satmovie_module(args.libsaturn)
    work = args.out / 'work'
    work.mkdir(parents=True, exist_ok=True)
    handle, image, lba, movies = psx_movie.open_movies(args.track1)
    try:
        endings = [m for m in movies[:ENDING_MOVIES]]
        if len(movies) < ENDING_MOVIES + 2 or movies[ENDING_MOVIES].frames != 1950:
            raise SystemExit(f'expected twenty endings before the opening, found {len(movies)} movies')
        sectors = max(m.last for m in movies) + 1
        total = sum(pack(sat, image, lba, movies, sectors, c, args.quality, args.out, work)
                    for c in (args.only or sorted(ENDINGS)))
    finally:
        image.close()
        handle.close()
    for leftover in work.iterdir():
        leftover.unlink()
    work.rmdir()
    print(f'{total / 1048576:.1f} MiB in {args.out}; {len(endings)} ending STRs on the disc')


if __name__ == '__main__':
    main()
