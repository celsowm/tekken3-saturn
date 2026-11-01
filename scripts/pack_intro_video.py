#!/usr/bin/env python3
"""Cut the opening movie from the USA disc and pack it as INTRO.MOV for the Saturn.

The PS1 movie is an STR/MDEC stream. ffmpeg decodes it (-f psxstr), scales it to the picture size and
encodes it with its Cinepak encoder, one strip per frame. The Saturn decoder (LibSaturn saturn/movie.h)
reads that stream as it is and converts each codebook entry to VDP2 dots itself. The sound is the movie's
XA audio as 22.05 kHz stereo IMA ADPCM, interleaved with the frames a little ahead of the moment it plays.

Commands:
  probe   encode a window at each candidate picture size and print bytes per frame (gate 0)
  build   write the movie file
Private output: build/intro-* (git-ignored). The movie is game data and never goes into git.
"""
import argparse
import struct
import subprocess
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
MAGIC = b'SATMOV01'
HEADER_BYTES = 2048
AUDIO_RATE, AUDIO_CHANNELS, AUDIO_BLOCK_FRAMES = 22050, 2, 1024
AUDIO_LEAD_FRAMES = 12              # a block travels this many frames ahead of the moment it starts to play
DRIVE_BYTES_PER_SECOND = 150 * 2048 * 9 // 10   # a 2x drive at 150 sectors a second, less 10% for retries
PREROLL_BYTES = 24 * 2048                        # what the player has when its clock starts (intro_main.c PREROLL_SECTORS)
SECTORS_PER_SECOND = 150
IMA_STEPS = (7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
             88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
             658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
             3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
             16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767)
IMA_INDEX_DELTA = (-1, -1, -1, -1, 2, 4, 6, 8)


def ffmpeg(*args):
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', *map(str, args)], check=True)


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


def encode(source, size, quality, out, seconds=None):
    w, h = size
    window = ['-t', seconds] if seconds else []
    ffmpeg('-f', 'psxstr', '-i', source, *window, '-an', '-fps_mode', 'passthrough',
           '-vf', f'scale={w}:{h}:flags=lanczos', '-pix_fmt', 'rgb24',
           '-c:v', 'cinepak', '-q:v', quality, '-max_strips', 1, '-skip_empty_cb', 1, out)
    return out.stat().st_size


def check_frame(frame):
    """The Saturn decoder reads one strip of colour chunks; a frame that is not is an error here, not in the game."""
    width, height, strips = struct.unpack_from('>HHH', frame, 4)
    if strips != 1:
        raise ValueError(f'{strips} strips: encode with -max_strips 1')
    end, pos = 10 + struct.unpack_from('>H', frame, 12)[0], 22
    while end - pos >= 4:
        if 0x24 <= frame[pos] <= 0x27:
            raise ValueError('grey chunk')
        pos += struct.unpack_from('>H', frame, pos + 2)[0]
    return width, height


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


def decode_audio(stream, out):
    ffmpeg('-f', 'psxstr', '-i', stream, '-map', '0:a:0', '-ar', AUDIO_RATE, '-ac', AUDIO_CHANNELS, '-f', 's16le', out)
    return out.read_bytes()


def ima_encode(pcm, frames_wanted):
    """Stereo S16LE to blocked IMA ADPCM, `frames_wanted` frames (silence pads or the end cuts)."""
    import array
    samples = array.array('h')
    samples.frombytes(pcm[:frames_wanted * 4])
    samples.extend([0] * (frames_wanted * 2 - len(samples)))
    blocks, index = [], [0, 0]
    predictor = [0, 0]
    for first in range(0, frames_wanted, AUDIO_BLOCK_FRAMES):
        block = bytearray()
        for c in (0, 1):
            predictor[c] = samples[first * 2 + c] if first * 2 + c < len(samples) else 0
            block += struct.pack('>hBB', predictor[c], index[c], 0)
        for f in range(1, AUDIO_BLOCK_FRAMES):
            byte = 0
            for c in (0, 1):
                at = (first + f) * 2 + c
                target = samples[at] if at < len(samples) else 0
                diff, code = target - predictor[c], 0
                if diff < 0:
                    code, diff = 8, -diff
                step = IMA_STEPS[index[c]]
                if diff >= step:
                    code |= 4
                    diff -= step
                if diff >= step >> 1:
                    code |= 2
                    diff -= step >> 1
                if diff >= step >> 2:
                    code |= 1
                delta = step >> 3
                delta += step if code & 4 else 0
                delta += step >> 1 if code & 2 else 0
                delta += step >> 2 if code & 1 else 0
                predictor[c] = max(-32768, min(32767, predictor[c] - delta if code & 8 else predictor[c] + delta))
                index[c] = max(0, min(88, index[c] + IMA_INDEX_DELTA[code & 7]))
                byte = byte << 4 | code
            block.append(byte)
        blocks.append(bytes(block).ljust(4 * ((len(block) + 3) // 4), b'\0'))
    return blocks


def audio_for_frames(stream, frames):
    """The audio blocks to carry in each video packet: a list of lists, one per frame."""
    raw = stream.read_bytes()
    start = audio_start_seconds(raw)
    pcm = decode_audio(stream, WORK / 'opening.pcm')
    WORK.joinpath('opening.pcm').unlink()
    if start >= 0:
        pcm = b'\0' * (round(start * AUDIO_RATE) * 4) + pcm
    else:
        pcm = pcm[round(-start * AUDIO_RATE) * 4:]
    total = -(-frames * FRAME_US * AUDIO_RATE // 1000000 // AUDIO_BLOCK_FRAMES) * AUDIO_BLOCK_FRAMES
    carried = [[] for _ in range(frames)]
    for b, block in enumerate(ima_encode(pcm, total)):
        due = b * AUDIO_BLOCK_FRAMES * 1000000 // AUDIO_RATE // FRAME_US          # the frame it starts to play in
        carried[max(0, min(frames - 1, due - AUDIO_LEAD_FRAMES))].append(block)
    return carried, start

def split_frames(avi, out_dir):
    """The encoder's packets, one per frame, in order."""
    raw = out_dir / (avi.stem + '.cvid')
    ffmpeg('-i', avi, '-c:v', 'copy', '-f', 'rawvideo', raw)
    sizes = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v', '-show_entries', 'packet=size',
                            '-of', 'csv=p=0', avi], check=True, capture_output=True, text=True).stdout.split()
    data, frames, pos = raw.read_bytes(), [], 0
    for size in map(int, sizes):
        frames.append(data[pos:pos + size])
        pos += size
    raw.unlink()
    return frames


def drive_slack(packet_sizes, frame_us=FRAME_US, rate=DRIVE_BYTES_PER_SECOND, preroll=PREROLL_BYTES):
    """Fewest bytes the drive is ahead of the player, as the player consumes packet k at frame k's time.

    Negative means the player would wait for the disc somewhere. It counts the preroll and the drive's
    rate, not the CD Block's 200-sector buffer, which is extra cushion on top."""
    total, worst = 0, None
    for k, size in enumerate(packet_sizes):
        total += size
        slack = preroll + rate * k * frame_us / 1000000 - total
        worst = slack if worst is None else min(worst, slack)
    return worst


def worst_window_rate(packet_sizes, seconds=10, frame_us=FRAME_US):
    """Highest data rate over any `seconds` of the movie, in bytes a second."""
    window = max(1, seconds * 1000000 // frame_us)
    window = min(window, len(packet_sizes))
    return max(sum(packet_sizes[i:i + window]) for i in range(len(packet_sizes) - window + 1)) * 1000000 / (window * frame_us)


def write_movie(path, size, frames, audio):
    packets, longest, sizes = bytearray(), 0, []
    for frame, blocks in zip(frames, audio):
        sound = b''.join(blocks)
        packet = struct.pack('>II', len(frame), len(sound)) + sound + frame + b'\0' * (-len(frame) % 4)
        longest = max(longest, len(packet))
        packets += packet
        sizes.append(len(packet))
    slack = drive_slack(sizes)
    if slack < 0:
        raise SystemExit(f'the drive cannot keep up: {-slack / 1024:.0f} KiB short at 90% of 2x; lower --quality or the size')
    print(f'data rate: mean {len(packets) * 1000000 / (len(frames) * FRAME_US) / 1024:.0f} KiB/s, worst 10 s '
          f'{worst_window_rate(sizes) / 1024:.0f} KiB/s of {150 * 2048 / 1024:.0f} at 2x; ahead of the drive by at least {slack / 1024:.0f} KiB')
    header = MAGIC + struct.pack('>HHIIIBBHII', size[0], size[1], len(frames), FRAME_US, AUDIO_RATE, AUDIO_CHANNELS, 1,
                                 AUDIO_BLOCK_FRAMES, longest, len(packets))
    path.write_bytes(header.ljust(HEADER_BYTES, b'\0') + packets)


def source(args):
    WORK.mkdir(parents=True, exist_ok=True)
    stream = WORK / 'opening.str'
    if not stream.exists():
        movie = extract_opening(args.track1, stream)
        print(f'opening: sectors {movie.first}..{movie.last}, {movie.frames} frames, {stream.stat().st_size >> 20} MiB raw')
    return stream


def probe(args):
    stream = source(args)
    for name, size in SIZES.items():
        out = WORK / f'op-{name}.avi'
        nbytes = encode(stream, size, args.quality, out, args.seconds)
        frames = len(split_frames(out, WORK))
        print(f'{name}: {nbytes / frames / 1024:5.1f} KiB/frame over {frames} frames')
        out.unlink()


def build(args):
    stream = source(args)
    size = SIZES[args.size]
    avi = WORK / f'op-{args.size}-q{args.quality}.avi'
    if args.fresh or not avi.exists():
        encode(stream, size, args.quality, avi)
    rewritten = split_frames(avi, WORK)
    for frame in rewritten:
        got = check_frame(frame)
        if got != size:
            raise SystemExit(f'frame is {got}, expected {size}')
    if len(rewritten) != OPENING_FRAMES:
        raise SystemExit(f'{len(rewritten)} frames, expected {OPENING_FRAMES}')
    audio, start = audio_for_frames(stream, len(rewritten))
    write_movie(args.out, size, rewritten, audio)
    total = sum(len(f) for f in rewritten)
    print(f'{args.out}: {len(rewritten)} frames, mean {total / len(rewritten) / 1024:.1f} KiB, '
          f'max {max(map(len, rewritten)) / 1024:.1f} KiB, {args.out.stat().st_size / 1048576:.1f} MiB; '
          f'audio starts {start * 1000:.0f} ms after the first frame')


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--track1', type=Path, default=psx_movie.TRACK1)
    sub = p.add_subparsers(dest='command', required=True)
    q = sub.add_parser('probe', help='encode a window at each picture size and print bytes per frame')
    q.add_argument('--seconds', type=int, default=20)
    q.add_argument('--quality', type=int, default=2)
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
