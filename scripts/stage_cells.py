"""The panorama cylinder as VDP2 cells: 4 bits a dot, 8 x 8 dots a character, 16 palettes of 15 colours.

`scripts/build_stage_packs.py` stores a stage's backdrop this way (format version 4) and
`scripts/backdrop_format_study.py` measures it. Each cell takes the palette that suits it best (a
median cut over the cells' mean colours, then k-means style reassignment); equal cells (same palette,
same dots) share one character, and when more than `max_chars` remain the cheapest ones are merged
into their nearest neighbour of the same palette. Character 0 is always the transparent cell.
"""
import numpy as np

CELL = 8
PALETTES, PALETTE_COLOURS = 16, 15


def channels(colour):
    """RGB555 (bit 15 set when opaque) to 5-bit channels, last axis red, green, blue."""
    return np.stack([(colour >> (5*k)) & 31 for k in range(3)], axis=-1).astype(np.int32)


def quantize(pixels, count):
    """Palette (count x 3, 5-bit channels) of `count` colours for the pixels, by median cut with k-means."""
    from PIL import Image
    if len(pixels) == 0:
        return np.zeros((count, 3), np.int32)
    strip = Image.fromarray((pixels*8).astype(np.uint8).reshape(1, -1, 3), 'RGB')
    q = strip.quantize(colors=count, method=Image.Quantize.MEDIANCUT, kmeans=4, dither=Image.Dither.NONE)
    levels = np.array(q.getpalette()[:count*3]).reshape(-1, 3)
    levels = np.minimum(31, (levels + 4)//8)
    return np.concatenate([levels, np.zeros((count - len(levels), 3), np.int64)]).astype(np.int32)


def cells_of(array, rows, columns):
    """(rows/8 * columns/8, 8, 8, ...) cells in row-major order."""
    return array.reshape(rows//CELL, CELL, columns//CELL, CELL, *array.shape[2:]).swapaxes(1, 2) \
        .reshape(-1, CELL, CELL, *array.shape[2:])


def cluster(rgb, opaque, rows, columns, rounds=4):
    """(palettes of 15 colours each, the palette of every cell, the squared error)."""
    pix = cells_of(rgb, rows, columns).reshape(-1, CELL*CELL, 3)
    op = cells_of(opaque, rows, columns).reshape(-1, CELL*CELL)
    count = len(pix)
    has = op.any(1)
    mean = np.array([p[o].mean(0) if o.any() else (0, 0, 0) for p, o in zip(pix, op)])
    # Start: split the cells by mean colour (median cut over cells).
    groups = np.zeros(count, np.int64)
    for _ in range(4):
        new = groups.copy()
        for g in range(int(groups.max()) + 1):
            members = np.nonzero((groups == g) & has)[0]
            if len(members) < 2:
                continue
            axis = mean[members].var(0).argmax()
            cut = np.median(mean[members][:, axis])
            new[members[mean[members][:, axis] > cut]] = int(groups.max()) + 1 + g
        _, groups = np.unique(new, return_inverse=True)
    groups = np.minimum(groups, PALETTES - 1)
    total = 0.0
    for _ in range(rounds):
        palettes = []
        for g in range(PALETTES):
            members = np.nonzero((groups == g) & has)[0]
            p = pix[members][op[members]]
            palettes.append(quantize(p, PALETTE_COLOURS) if len(p) else np.zeros((PALETTE_COLOURS, 3), np.int32))
        err = np.zeros((count, PALETTES))
        for g, pal in enumerate(palettes):
            d = ((pix[:, :, None, :] - pal[None, None, :, :])**2).sum(-1).min(-1)
            err[:, g] = (d*op).sum(1)
        groups = err.argmin(1)
        total = float(err[np.arange(count), groups][has].sum())
    return np.array(palettes), groups, total


def dots_of(rgb, opaque, rows, columns, palettes, groups):
    """Palette index (0 transparent, 1-15 the colours) of every dot of every cell: (cells, 64)."""
    pix = cells_of(rgb, rows, columns).reshape(-1, CELL*CELL, 3)
    op = cells_of(opaque, rows, columns).reshape(-1, CELL*CELL)
    dots = np.zeros((len(pix), CELL*CELL), np.uint8)
    for g, pal in enumerate(palettes):
        members = np.nonzero(groups == g)[0]
        if len(members):
            d = ((pix[members][:, :, None, :] - pal[None, None, :, :])**2).sum(-1)
            dots[members] = np.where(op[members], d.argmin(-1) + 1, 0)
    return dots


def merge(dots, groups, count, palettes, max_chars):
    """Reduce the unique cells to `max_chars` by folding the cheapest into their nearest neighbour
    of the same palette. Returns the map from old to new cell number and the survivors (old numbers)."""
    n = len(dots)
    target = np.arange(n)
    alive = np.ones(n, bool)
    weight = count.astype(np.float64).copy()
    # Colour of every dot of every cell, transparency far from any colour.
    colour = np.zeros((n, CELL*CELL, 4), np.float64)
    for g in range(PALETTES):
        at = np.nonzero(groups == g)[0]
        full = np.concatenate([np.zeros((1, 3)), palettes[g]]).astype(np.float64)
        colour[at, :, :3] = full[dots[at]]
        colour[at, :, 3] = np.where(dots[at] == 0, 40.0, 0.0)
    distance = {}
    for g in range(PALETTES):
        at = np.nonzero(groups == g)[0]
        if len(at) < 2:
            continue
        x = colour[at].reshape(len(at), -1)
        sq = (x*x).sum(1)
        d = sq[:, None] + sq[None, :] - 2*x @ x.T
        np.fill_diagonal(d, np.inf)
        distance[g] = (at, np.maximum(d, 0))
    excess = int(alive.sum()) - max_chars
    while excess > 0:
        best = []
        for g, (at, d) in distance.items():
            live = alive[at]
            if live.sum() < 2:
                continue
            masked = np.where(live[None, :], d, np.inf)
            near = masked.argmin(1)
            cost = masked[np.arange(len(at)), near]*weight[at]
            for i in np.nonzero(live)[0]:
                best.append((cost[i], at[i], at[near[i]]))
        best.sort(key=lambda t: t[0])
        taken = set()
        for cost, cell, to in best:
            if excess <= 0:
                break
            if cell in taken or to in taken or not np.isfinite(cost):
                continue
            taken.add(cell)
            taken.add(to)
            alive[cell] = False
            target[cell] = to
            weight[to] += weight[cell]
            excess -= 1
        if not taken:
            raise ValueError('cannot merge cells down to the character limit')
    # Follow merge chains to a surviving cell.
    for i in range(n):
        while not alive[target[i]]:
            target[i] = target[target[i]]
    return target, np.nonzero(alive)[0]


def cell_picture(colour, columns, rows, max_chars):
    """The cylinder (RGB555 with bit 15 set when opaque, 0 transparent, rows x columns) as cells.

    Returns dict(palettes: 16 x 16 RGB555 words (entry 0 of each is 0), chars: n x 64 dot indices,
    names: (rows/8, columns/8) character numbers, palette_of: n palette numbers, picture: the
    result as 5-bit RGB rows x columns x 3, with its RMS error against the input in 5-bit steps)."""
    colour = np.asarray(colour).reshape(rows, columns)
    opaque = colour != 0
    rgb = channels(colour)
    palettes, groups, _ = cluster(rgb, opaque, rows, columns)
    dots = dots_of(rgb, opaque, rows, columns, palettes, groups)
    # Equal cells share a character: key = (palette, dots); every transparent cell is one cell.
    groups = np.where(dots.any(1), groups, 0)
    keyed = np.concatenate([groups[:, None].astype(np.uint8), dots], axis=1)
    unique, first, inverse, count = np.unique(keyed, axis=0, return_index=True, return_inverse=True,
                                              return_counts=True)
    inverse = inverse.reshape(-1)
    u_groups, u_dots = unique[:, 0].astype(np.int64), unique[:, 1:]
    blank = np.nonzero(~u_dots.any(1))[0]
    if len(blank) == 0:
        raise ValueError('no transparent cell: the picture has no sky')
    # Merge down to the character limit (the blank cell stays and is character 0).
    if len(unique) > max_chars:
        weight = count.copy()
        weight[blank[0]] = 1 << 40
        target, alive = merge(u_dots, u_groups, weight, palettes, max_chars)
    else:
        target, alive = np.arange(len(unique)), np.arange(len(unique))
    order = np.concatenate([blank[:1], alive[alive != blank[0]]])
    number = np.zeros(len(unique), np.int64)
    number[order] = np.arange(len(order))
    names = number[target[inverse]].reshape(rows//CELL, columns//CELL)
    chars, palette_of = u_dots[order], u_groups[order]
    # The picture these characters show, for the error and the previews.
    full = np.concatenate([np.zeros((PALETTES, 1, 3), np.int32), palettes], axis=1)
    shown_cells = full[palette_of[names.reshape(-1)][:, None], chars[names.reshape(-1)]]
    shown = shown_cells.reshape(rows//CELL, columns//CELL, CELL, CELL, 3).swapaxes(1, 2).reshape(rows, columns, 3)
    err = float((((shown - rgb)**2).sum(-1)[opaque]).mean()/3)**.5 if opaque.any() else 0.0
    words = np.zeros((PALETTES, 16), np.int64)
    words[:, 1:] = 0x8000 | palettes[..., 0] | palettes[..., 1] << 5 | palettes[..., 2] << 10
    return dict(palettes=words, chars=chars, names=names, palette_of=palette_of, picture=shown, rms=err,
                unique=len(unique))
