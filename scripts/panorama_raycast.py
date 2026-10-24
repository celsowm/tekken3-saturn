"""Ray casting into the private stage panorama, for the cylinder bake and its checks.

The panorama is placed as the original draws it (Tekken3Remake
docs/research/code/stages.md): the TMD scaled by 8 around the fighters'
midpoint and lowered by the per-stage height, in PS1 axes (y down). Rays are
given in that local frame (the turn already removed). A ray takes the nearest
hit whose texel is not transparent; hits within TIE of the nearest go to the
lower packet, which the original draws last (it links the packets into one
ordering-table bucket in reverse order).
"""
import numpy as np

from build_local_paul_stage import primitives

SCALE = 8
TIE = 0.005


class Panorama:
    def __init__(self, tmd, vram, height):
        tris, uvs, pages, cluts, rgbs, order = [], [], [], [], [], []
        for index, (rgb, material, corners) in enumerate(primitives(tmd)):
            if material is None:
                raise ValueError('untextured panorama packets are not supported')
            page, clut = material
            if (page >> 7) & 3 or page & ~0x1ff or clut & 0x8000:
                raise ValueError('invalid 4-bit stage material')
            points = [(SCALE*x, SCALE*y + height, SCALE*z) for (x, y, z), _ in corners]
            for ids in ([(0, 1, 2)] if len(corners) == 3 else [(0, 1, 2), (1, 3, 2)]):
                tris.append([points[i] for i in ids])
                uvs.append([corners[i][1] for i in ids])
                pages.append(page)
                cluts.append(clut)
                rgbs.append(rgb)
                order.append(index)
        self.tris = np.array(tris, dtype=np.float64)
        self.uvs = np.array(uvs, dtype=np.float64)
        self.pages = np.array(pages)
        self.cluts = np.array(cluts)
        self.rgbs = np.array([[c for c in rgb] for rgb in rgbs], dtype=np.int64)
        self.order = np.array(order)
        self.vram = np.array(vram, dtype=np.int64)
        self.half = float(np.abs(self.tris[:, :, [0, 2]]).max())

    def _texels(self, k, u, v):
        """RGB555 colours (0 transparent) of triangle k at texel coordinates u, v."""
        page, clut = self.pages[k], self.cluts[k]
        u, v = u.astype(np.int64) & 255, v.astype(np.int64) & 255
        word = self.vram[(((page >> 4) & 1)*256 + v)*1024 + (page & 15)*64 + u//4]
        index = (word >> ((u & 3)*4)) & 15
        colour = self.vram[((clut >> 6) & 511)*1024 + (clut & 63)*16 + index]
        rgb = self.rgbs[k]
        out = np.zeros_like(colour)
        for channel in range(3):
            value = np.minimum(31, ((colour >> (5*channel)) & 31)*rgb[channel]//128)
            out |= value << (5*channel)
        return np.where(colour == 0, 0, out | 0x8000)

    def cast(self, origins, dirs):
        """Nearest visible hits: (distance along dir, RGB555 colour or 0, hit point)."""
        origins = np.broadcast_to(np.asarray(origins, dtype=np.float64), dirs.shape)
        best = np.full(len(dirs), np.inf)
        rank = np.full(len(dirs), 1 << 30)
        colour = np.zeros(len(dirs), dtype=np.int64)
        for k in range(len(self.tris)):
            v0, v1, v2 = self.tris[k]
            e1, e2 = v1 - v0, v2 - v0
            p = np.cross(dirs, e2)
            det = p @ e1
            ok = np.abs(det) > 1e-9
            inv = np.where(ok, 1.0/np.where(ok, det, 1.0), 0.0)
            s = origins - v0
            b = (s*p).sum(1)*inv
            q = np.cross(s, e1)
            c = (dirs*q).sum(1)*inv
            t = (q @ e2)*inv
            near = best*(1 - TIE)
            tie = (t <= best*(1 + TIE)) & (self.order[k] < rank)
            hit = ok & (b >= -1e-7) & (c >= -1e-7) & (b + c <= 1 + 1e-7) & (t > 1e-6) & ((t < near) | tie)
            if not hit.any():
                continue
            at = np.nonzero(hit)[0]
            w = 1 - b[at] - c[at]
            uv = self.uvs[k]
            u = np.rint(w*uv[0, 0] + b[at]*uv[1, 0] + c[at]*uv[2, 0])
            v = np.rint(w*uv[0, 1] + b[at]*uv[1, 1] + c[at]*uv[2, 1])
            texel = self._texels(k, u, v)
            seen = texel != 0
            at = at[seen]
            best[at] = t[at]
            rank[at] = self.order[k]
            colour[at] = texel[seen]
        points = origins + dirs*np.where(np.isfinite(best), best, 0)[:, None]
        return best, colour, points


def cylinder_dirs(columns, rows, tan_top, rows_per_tan):
    """Unit-free ray directions (PS1 axes) of the cylinder texel centres, row-major."""
    phi = 2*np.pi*(np.arange(columns) + 0.5)/columns
    tan = tan_top - (np.arange(rows) + 0.5)/rows_per_tan
    x = np.broadcast_to(np.sin(phi)[None, :], (rows, columns))
    z = np.broadcast_to(np.cos(phi)[None, :], (rows, columns))
    y = np.broadcast_to(-tan[:, None], (rows, columns))
    return np.stack([x, y, z], axis=-1).reshape(-1, 3)
