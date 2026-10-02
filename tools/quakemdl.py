"""Quake MDL (version 6) models into game-scene models (tools/scene_arena.py).

Reads the header, skin, texture coordinates (seam vertices duplicated for
back-side triangles, as Quake did), triangles and simple or grouped frames.
Each frame becomes VANM data in the runtime's own axes: Quake models face
+x with z up; DOSBench models face +z with y up, so (x, y, z) =
(Quake y, Quake z, Quake x). Normals are not taken from the file's indices
into Quake's normal table: they are computed from each frame's own
triangles and quantised to the scene's 256-entry NRML table. The skin goes
through the palette and is resampled to power-of-two sides. Standard
library only.
"""
import struct

import dbs
import gsgen as G


class Mdl:
    def __init__(self, data, palette):
        h = struct.unpack_from("<4si3f3f f3f iii iii ii f", data)
        if h[0] != b"IDPO" or h[1] != 6:
            raise ValueError("not a version 6 MDL")
        self.scale, self.origin = h[2:5], h[5:8]
        nskins, sw, sh, nverts, ntris, nframes = h[12], h[13], h[14], h[15], h[16], h[17]
        o = struct.calcsize("<4si3f3f f3f iii iii ii f")
        self.skin_w, self.skin_h = sw, sh
        self.skin = None
        for _ in range(nskins):
            kind = struct.unpack_from("<i", data, o)[0]
            o += 4
            if kind == 0:
                pix = data[o:o + sw * sh]
                o += sw * sh
            else:                                       # a group: take its first picture
                n = struct.unpack_from("<i", data, o)[0]
                o += 4 + 4 * n
                pix = data[o:o + sw * sh]
                o += sw * sh * n
            if self.skin is None:
                self.skin = bytes(c for i in pix for c in (palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2], 255))
        self.st = [struct.unpack_from("<3i", data, o + 12 * i) for i in range(nverts)]
        o += 12 * nverts
        self.tris = [struct.unpack_from("<4i", data, o + 16 * i) for i in range(ntris)]
        o += 16 * ntris
        self.frames, self.names = [], []
        for _ in range(nframes):
            kind = struct.unpack_from("<i", data, o)[0]
            o += 4
            count = 1
            if kind != 0:
                count = struct.unpack_from("<i", data, o)[0]
                o += 4 + 8 + 4 * count                  # count, bboxes, intervals
            for _ in range(count):
                o += 8                                  # bbox min, max
                name = data[o:o + 16].split(b"\0")[0].decode("latin-1")
                o += 16
                self.frames.append(data[o:o + 4 * nverts])
                self.names.append(name)
                o += 4 * nverts

    def sequence(self, prefix):
        """(first, count) of the frames named prefix1, prefix2, ... (Quake's naming)."""
        idx = [i for i, n in enumerate(self.names) if n.rstrip("0123456789") == prefix]
        if not idx:
            raise KeyError("no frames named %s*" % prefix)
        return idx[0], idx[-1] - idx[0] + 1


def add_mdl(s, mdl, size=256, frames=None, colour=(255, 255, 255)):
    """The MDL as a lit, animated model of s (one batch, its skin as a texture).
    frames: frame indices to keep (default all). Returns (model, texture, frame map)."""
    keep = frames if frames is not None else list(range(len(mdl.frames)))
    tw, th, trgba = dbs.fit_texture(mdl.skin_w, mdl.skin_h, mdl.skin, size)
    tex = s.add_texture(tw, th, trgba, dbs.TF_MIPMAP)
    # Output vertices: (source vertex, back-side seam) pairs.
    out, index = [], {}
    tris = []
    for front, a, b, c in mdl.tris:
        t = []
        for v in (a, b, c):
            seam = (not front) and mdl.st[v][0] != 0
            key = (v, seam)
            if key not in index:
                index[key] = len(out)
                out.append(key)
            t.append(index[key])
        tris.append((t[0], t[2], t[1]))                 # Quake winds clockwise from the front
    sx, sy, sz = mdl.scale
    ox, oy, oz = mdl.origin

    def pos(frame, v):
        x, y, z = frame[4 * v], frame[4 * v + 1], frame[4 * v + 2]
        return (sy * y + oy, sz * z + oz, sx * x + ox)

    def frame_normals(frame):
        p = [pos(frame, v) for v, _ in out]
        acc = [(0.0, 0.0, 0.0)] * len(out)
        for a, b, c in tris:
            n = G.cross(G.sub(p[b], p[a]), G.sub(p[c], p[a]))
            for i in (a, b, c):
                acc[i] = G.add(acc[i], n)
        # Seam copies share their source's normal.
        by_src = {}
        for i, (v, _) in enumerate(out):
            by_src[v] = G.add(by_src.get(v, (0.0, 0.0, 0.0)), acc[i])
        return [G.norm(by_src[v]) for v, _ in out]

    table = dbs.sphere_normals()
    f0 = mdl.frames[keep[0]]
    verts = []
    for v, seam in out:
        p = pos(f0, v)
        u = (mdl.st[v][1] + (mdl.skin_w / 2 if seam else 0) + 0.5) / mdl.skin_w
        t = (mdl.st[v][2] + 0.5) / mdl.skin_h
        verts.append((p[0], p[1], p[2], colour[0], colour[1], colour[2], 255, u, t, 0.0, 0.0))
    vfirst = len(s.verts)
    s.add_batch(verts, tris, tex=tex)
    s.set_normals(vfirst, frame_normals(f0))
    data = bytearray()
    for k in keep:
        fr = mdl.frames[k]
        ns = frame_normals(fr)
        for i, (v, _) in enumerate(out):
            data += bytes((fr[4 * v + 1], fr[4 * v + 2], fr[4 * v], dbs.nearest_normal(table, ns[i])))
    s.anims.append(dict(nverts=len(out), nframes=len(keep), vfirst=vfirst, scale=(sy, sz, sx), origin=(oy, oz, ox),
                        data=bytes(data)))
    m = s.add_model(len(s.batches) - 1, 1, flags=dbs.MF_LIT | dbs.MF_ANIM, anim=len(s.anims) - 1)
    return m, tex, {k: i for i, k in enumerate(keep)}


def load(member):
    """(Mdl, palette) for a file in LibreQuake's pak0 (lite.zip)."""
    import assets
    import bsp
    data, palette = bsp.load_from_lite(assets.src(bsp.LITE), member)
    return Mdl(data, palette), palette
