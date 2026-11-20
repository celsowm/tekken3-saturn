#!/usr/bin/env python3
"""Cut the V0.5 showcase: the fighters' Gouraud lighting from V0.4 to V0.5, Tekken Ball, Practice, the links.

Takes (scripts/record_ymir_video.py, all on Ymir from the same pad script, one folder, --takes):
  gouraud-before  the V0.4 disc, arcade round 1 from field 4355 (scripts/extract_disc_file.py gets its 0.BIN out of the zip)
  gouraud-after   the V0.5 disc, the same round from field 4400: V0.4 reaches the same moment 45 fields earlier, so the two
                  takes show the same poses and the same timer, and only the shading differs
  ball            mode list, select with the spinning ball, VS screen, the rally (ymir_ball_flow.py's pad script)
  practice-menus  FREE page, FREEZE SIGNAL tint, the command list (ymir_practice_flow.py --flow menus)
  practice-recorder  the input recorder and its replay (--flow recorder)

The Gouraud story is a V0.4 shot, a divider that sweeps across it and reveals V0.5 on the same frame, then zoomed
V0.4 | V0.5 pairs on each fighter. The sound of every Gouraud part is the V0.5 take's. The labels are PNGs (this ffmpeg
has no drawtext). The release name is read from src/saturn/version.h; the end card carries the links. Output: --out
(full cut) and --short (the Gouraud story and a taste of the modes, under X's 2:20 for a standard account).
Spans were read from contact sheets of the takes; a new take needs new spans.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from edit_arcade_video import FONT, H, LINKS, W, card
from release_version import version

WIPE = 3.5                      # seconds the divider takes to cross the picture
# (take, start, end) of the Gouraud takes, in seconds of both.
BEFORE_ONLY, WIPE_SPAN, AFTER_ONLY, ZOOM_XIAOYU, ZOOM_PAUL = (0, 3), (3, 3 + WIPE), (6.5, 9), (9, 12), (12, 15)
# crop of the zoomed pair: width, height, x, y of the 960 x 720 picture, scaled to 480 x 720 each
ZOOM_CROP = {'xiaoyu': (400, 600, 520, 80), 'paul': (400, 600, 100, 80)}
FLAT, LIT = (255, 190, 90), (120, 230, 255)


def tag(path, items):
    """A transparent W x H picture with pill labels: items are (text, x, colour); the pill sits above the bottom edge.
    A negative x is the gap from the right edge (the game's own text fills the bottom-left of Practice)."""
    image = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(FONT, 34)
    for text, x, colour in items:
        width = draw.textlength(text, font=font)
        if x < 0:
            x = W + x - width - 36
        draw.rounded_rectangle((x, H - 96, x + width + 36, H - 38), 14, fill=(0, 0, 0, 190), outline=colour + (255,), width=3)
        draw.text((x + 18, H - 90), text, font=font, fill=colour + (255,))
    image.save(path)


def part(out, inputs, graph, seconds):
    """One part, re-encoded alike (960 x 720, 60 fps, AAC) so the parts concatenate without re-encoding."""
    fade = f'afade=t=in:d=0.04,afade=t=out:st={seconds - 0.04:.3f}:d=0.04'
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', *inputs, '-filter_complex', graph.format(fade=fade),
                    '-map', '[v]', '-map', '[a]', '-c:v', 'libx264', '-crf', '20', '-preset', 'medium', '-pix_fmt', 'yuv420p',
                    '-c:a', 'aac', '-b:a', '160k', '-ar', '44100', '-ac', '2', str(out)], check=True)


FINISH = 'scale=960:720:flags=lanczos,setsar=1,fps=60,format=yuv420p'


def window(path, span):
    return ['-ss', str(span[0]), '-to', str(span[1]), '-i', str(path)]


def gouraud(work, takes):
    """The Gouraud parts, in order: V0.4, the wipe, V0.5, two zoomed pairs. Sound from the V0.5 take."""
    before, after = takes['gouraud-before'], takes['gouraud-after']
    tags = {}
    for name, items in (('flat', [('V0.4  flat colours', 30, FLAT)]), ('lit', [('V0.5  Gouraud lit', 30, LIT)]),
                        ('wipe', [('V0.5  Gouraud lit', 30, LIT), ('V0.4  flat colours', W - 340, FLAT)]),
                        ('pair', [('V0.4  flat', 30, FLAT), ('V0.5  lit', W // 2 + 30, LIT)])):
        tags[name] = work / f'tag-{name}.png'
        tag(tags[name], items)
    parts = []

    def make(name, span, tag_name, graph):
        out = work / f'gouraud-{name}.mp4'
        seconds = span[1] - span[0]
        inputs = window(before, span) + window(after, span) + ['-i', str(tags[tag_name])]
        part(out, inputs, graph, seconds)
        parts.append(out)
    make('0', BEFORE_ONLY, 'flat', f'[0:v][2:v]overlay=0:0,{FINISH}[v];[1:a]{{fade}}[a]')
    make('1', WIPE_SPAN, 'wipe',
         "[0:v]format=gbrp[a0];[1:v]format=gbrp[b0];"
         f"[a0][b0]blend=all_expr='if(lt(abs(X-W*T/{WIPE}),3),255,if(lt(X,W*T/{WIPE}),B,A))',format=yuv420p[w];"
         f'[w][2:v]overlay=0:0,{FINISH}[v];[1:a]{{fade}}[a]')
    make('2', AFTER_ONLY, 'lit', f'[1:v][2:v]overlay=0:0,{FINISH}[v];[1:a]{{fade}}[a]')
    for name, span, who in (('3', ZOOM_XIAOYU, 'xiaoyu'), ('4', ZOOM_PAUL, 'paul')):
        w, h, x, y = ZOOM_CROP[who]
        crop = f'crop={w}:{h}:{x}:{y},scale=480:720:flags=lanczos'
        make(name, span, 'pair', f'[0:v]{crop}[l];[1:v]{crop}[r];[l][r]hstack[s];[s][2:v]overlay=0:0,{FINISH}[v];[1:a]{{fade}}[a]')
    return parts


def clip(work, name, take, start, end, label=None):
    """A span of one take, with a label in the bottom-left corner."""
    out = work / f'{name}.mp4'
    inputs = window(take, (start, end))
    if label:
        png = work / f'{name}-tag.png'
        tag(png, [(label, -30, LIT)])
        inputs += ['-i', str(png)]
        graph = f'[0:v][1:v]overlay=0:0,{FINISH}[v];[0:a]{{fade}}[a]'
    else:
        graph = f'[0:v]{FINISH}[v];[0:a]{{fade}}[a]'
    part(out, inputs, graph, end - start)
    return out


def still(work, name, lines, seconds, size):
    png, out = work / f'{name}.png', work / f'{name}.mp4'
    card(png, lines, size)
    part(out, ['-loop', '1', '-t', str(seconds), '-i', str(png), '-f', 'lavfi', '-t', str(seconds), '-i',
               'anullsrc=r=44100:cl=stereo'], f'[0:v]{FINISH}[v];[1:a]anull[a]', seconds)
    return out


def build(work, takes, out, short):
    name = version()
    parts = [still(work, 'title', ['TEKKEN 3 for the Sega Saturn', f'{name}: lit fighters', 'Tekken Ball and Practice'], 3.5, 52)]
    parts += gouraud(work, takes)
    if short:
        parts += [clip(work, 'ball-rally', takes['ball'], 47.5, 54, 'NEW  TEKKEN BALL'),
                  clip(work, 'practice-signal', takes['practice-menus'], 7, 12, 'NEW  PRACTICE MODE')]
        links = 5
    else:
        parts += [clip(work, 'ball-list', takes['ball'], 0, 3.2, 'NEW  TEKKEN BALL'),
                  clip(work, 'ball-select', takes['ball'], 14.8, 19),
                  clip(work, 'ball-rally', takes['ball'], 47.5, 73),
                  clip(work, 'practice-menus', takes['practice-menus'], 0, 20, 'NEW  PRACTICE MODE'),
                  clip(work, 'practice-recorder', takes['practice-recorder'], 1, 15, 'PRACTICE  input recorder and replay'),
                  still(work, 'also', [f'Also new in {name}', 'Survival and Time Attack, with saved bests',
                                       'Redrawn stage backdrops'], 3.5, 40)]
        links = 7
    parts.append(still(work, 'links', LINKS, links, 34))
    listing = work / 'parts.txt'
    listing.write_text(''.join(f"file '{p}'\n" for p in parts))
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', listing, '-c', 'copy',
                    '-movflags', '+faststart', str(out)], check=True)
    seconds = float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
                                             '-of', 'csv=p=0', str(out)]))
    print(f'{out}: {seconds:.1f} s, {out.stat().st_size / 1048576:.1f} MiB')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--takes', type=Path, required=True, help='the folder with the five takes (<name>.mp4)')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--short', type=Path, required=True)
    args = p.parse_args()
    takes = {name: args.takes / f'{name}.mp4' for name in
             ('gouraud-before', 'gouraud-after', 'ball', 'practice-menus', 'practice-recorder')}
    for path in takes.values():
        if not path.is_file():
            p.error(f'missing take {path}')
    for out, short in ((args.out, False), (args.short, True)):
        with tempfile.TemporaryDirectory() as folder:
            build(Path(folder), takes, out, short)


if __name__ == '__main__':
    main()
