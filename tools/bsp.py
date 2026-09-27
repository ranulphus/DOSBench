#!/usr/bin/env python3
"""LibreQuake map conversion (BSP29) for DOSBench's level fly-through.

The world model's faces become triangle batches keyed by (texture, lightmap
atlas, kind). Every face keeps its own vertices: texture coordinates from
its texinfo, lightmap coordinates into a 128x128 atlas page (packed as
GLQuake did) and, for the baked mode, the lightmap sampled at the vertex as
its colour. Textures come from the BSP's miptex through the palette in
gfx/palette.lmp; the scene's textures are halved until they fit a 2 MB
Glide TMU (16-bit texels with mip chains, lightmaps as 8-bit intensity).

The VISL section carries what the runtime needs to draw only what the
camera can see, as Quake did: the BSP nodes, the leaves (bounds, first mark,
mark count, compressed PVS offset), the mark list (face numbers), per-face
index ranges, and the PVS bytes. Coordinates are converted from Quake's
z-up to DOSBench's y-up: (x, y, z) -> (x, z, -y).

The camera path is built from the map's own entities: from the player
start to the nearest unvisited item, weapon or monster, routed through a
grid of free points with room around them (tested against the BSP),
string-pulled, smoothed and resampled to equal steps. Torch, flame and
light entities become alpha-tested flame sprites.

Content: LibreQuake, BSD-3-Clause (docs/COPYING in the release).
"""
import io
import math
import os
import re
import struct
import zipfile

import dbs

LITE = "lite.zip"
LQ_CREDIT = "LibreQuake v0.09-beta (lite), the LibreQuake project"
LQ_LICENCE = "BSD-3-Clause"
SCENES = {
    "LQE0M1": ("bsp", {"map": "maps/lq_e0m1.bsp"}, [LITE], LQ_CREDIT + ", map lq_e0m1", LQ_LICENCE,
               "LibreQuake e0m1 fly-through"),
}
BUDGET = 2 * 1024 * 1024 - 64 * 1024        # a 2 MB TMU, less room for the flame sprite and slack
LM_PAGE = 128
LM_BOOST = 1.8                              # lightmap scale (no overbright blending on one TMU)
EYE = 22.0
STEP = 8.0                                  # units per frame along the path (Quake's run speed at 40 fps)
MAX_FRAMES = 1200

CONTENTS_EMPTY, CONTENTS_SOLID, CONTENTS_WATER, CONTENTS_SKY = -1, -2, -3, -6


def q2y(v):
    return (v[0], v[2], -v[1])


class Bsp:
    def __init__(self, data):
        self.d = data
        ver = struct.unpack_from("<i", data)[0]
        if ver != 29:
            raise ValueError("BSP version %d (only 29)" % ver)
        self.lumps = [struct.unpack_from("<2i", data, 4 + 8 * i) for i in range(15)]
        self.planes = self.array(1, "<4fi")
        self.verts = self.array(3, "<3f")
        self.nodes = self.array(5, "<i2h6h2H")
        self.texinfo = self.array(6, "<8f2i")
        self.faces = self.array(7, "<2hi2h4Bi")
        self.leaves = self.array(10, "<2i6h2H4B")
        self.marks = self.array(11, "<H")
        self.edges = self.array(12, "<2H")
        self.surfedges = self.array(13, "<i")
        self.models = self.array(14, "<9f7i")
        self.vis = self.lump(4)
        self.light = self.lump(8)
        self.entities = parse_entities(self.lump(0).decode("latin-1"))
        self.miptex = self.read_miptex()

    def lump(self, i):
        off, ln = self.lumps[i]
        return self.d[off:off + ln]

    def array(self, i, fmt):
        s = struct.Struct(fmt)
        b = self.lump(i)
        return [s.unpack_from(b, k * s.size) for k in range(len(b) // s.size)]

    def read_miptex(self):
        b = self.lump(2)
        n = struct.unpack_from("<i", b)[0]
        out = []
        for i in range(n):
            ofs = struct.unpack_from("<i", b, 4 + 4 * i)[0]
            if ofs < 0:
                out.append(None)
                continue
            name = b[ofs:ofs + 16].split(b"\0")[0].decode("latin-1").lower()
            w, h, o0 = struct.unpack_from("<3I", b, ofs + 16)
            out.append((name, w, h, b[ofs + o0:ofs + o0 + w * h]))
        return out

    def face_points(self, f):
        pts = []
        for k in range(f[3]):
            se = self.surfedges[f[2] + k][0]
            e = self.edges[abs(se)]
            pts.append(self.verts[e[0] if se >= 0 else e[1]])
        return pts

    def point_leaf(self, p):
        n = self.models[0][9]                   # headnode[0]
        while n >= 0:
            node = self.nodes[n]
            pl = self.planes[node[0]]
            d = pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3]
            n = node[1] if d >= 0 else node[2]
        return -1 - n

    def contents(self, p):
        return self.leaves[self.point_leaf(p)][0]

    def clear_line(self, a, b, step=8.0):
        d = math.dist(a, b)
        n = max(1, int(d / step))
        for i in range(n + 1):
            t = i / n
            p = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)
            if self.contents(p) in (CONTENTS_SOLID, CONTENTS_SKY):
                return False
        return True


def parse_entities(text):
    ents = []
    for block in re.findall(r"\{([^}]*)\}", text):
        ents.append(dict(re.findall(r'"([^"]*)"\s*"([^"]*)"', block)))
    return ents


def pak_files(pak):
    magic, off, ln = struct.unpack_from("<4sii", pak)
    if magic != b"PACK":
        raise ValueError("not a PAK")
    out = {}
    for i in range(ln // 64):
        name, fo, fl = struct.unpack_from("<56sii", pak, off + i * 64)
        out[name.split(b"\0")[0].decode("latin-1")] = (fo, fl)
    return out


def load_from_lite(path, member):
    with zipfile.ZipFile(path) as z:
        pak = z.read("lite/id1/pak0.pak")
    files = pak_files(pak)
    fo, fl = files[member]
    po, pl = files["gfx/palette.lmp"]
    return pak[fo:fo + fl], pak[po:po + pl]


# ---- textures -------------------------------------------------------------------
def miptex_rgba(pixels, palette, fence):
    out = bytearray(len(pixels) * 4)
    for i, p in enumerate(pixels):
        out[i * 4:i * 4 + 3] = palette[p * 3:p * 3 + 3]
        out[i * 4 + 3] = 0 if fence and p == 255 else 255
    return bytes(out)


def flame_texture(w=32, h=64):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            dx = (x - w / 2 + 0.5) / (w / 2)
            yy = y / h                               # 0 top, 1 bottom
            width = 0.15 + 0.85 * math.sin(math.pi * min(1.0, yy * 1.1)) ** 0.7 * (1 - 0.25 * yy)
            inside = abs(dx) < width * (0.55 + 0.45 * yy) and yy > 0.08
            core = abs(dx) < width * 0.35 * yy and yy > 0.35
            o = (y * w + x) * 4
            if inside:
                out[o:o + 4] = bytes((255, 250, 180, 255) if core else (255, int(110 + 120 * yy), 30, 255))
    return w, h, bytes(out)


# ---- lightmaps -------------------------------------------------------------------
class Atlas:
    """GLQuake's allocator: per page, the lowest column height that fits."""

    def __init__(self):
        self.pages = []                               # (heights[], texels bytearray)

    def alloc(self, w, h):
        for pi, (heights, _) in enumerate(self.pages):
            pos = self.fit(heights, w, h)
            if pos:
                return pi, pos
        self.pages.append(([0] * LM_PAGE, bytearray(LM_PAGE * LM_PAGE)))
        return len(self.pages) - 1, self.fit(self.pages[-1][0], w, h)

    @staticmethod
    def fit(heights, w, h):
        best, bx = LM_PAGE, None
        for x in range(LM_PAGE - w + 1):
            top = max(heights[x:x + w])
            if top < best:
                best, bx = top, x
        if bx is None or best + h > LM_PAGE:
            return None
        for x in range(bx, bx + w):
            heights[x] = best + h
        return bx, best


def face_extents(bsp, f):
    ti = bsp.texinfo[f[4]]
    ss, ts = [], []
    for p in bsp.face_points(f):
        ss.append(p[0] * ti[0] + p[1] * ti[1] + p[2] * ti[2] + ti[3])
        ts.append(p[0] * ti[4] + p[1] * ti[5] + p[2] * ti[6] + ti[7])
    bmin = (math.floor(min(ss) / 16), math.floor(min(ts) / 16))
    bmax = (math.ceil(max(ss) / 16), math.ceil(max(ts) / 16))
    return (bmin[0] * 16, bmin[1] * 16), ((bmax[0] - bmin[0]) + 1, (bmax[1] - bmin[1]) + 1)


def sample(light, ofs, w, h, s, t):
    """Bilinear lightmap sample at lightmap coordinates (s, t)."""
    s = min(max(s, 0.0), w - 1.0)
    t = min(max(t, 0.0), h - 1.0)
    x0, y0 = int(s), int(t)
    x1, y1 = min(x0 + 1, w - 1), min(y0 + 1, h - 1)
    fx, fy = s - x0, t - y0
    a = light[ofs + y0 * w + x0] * (1 - fx) + light[ofs + y0 * w + x1] * fx
    b = light[ofs + y1 * w + x0] * (1 - fx) + light[ofs + y1 * w + x1] * fx
    return a * (1 - fy) + b * fy


# ---- the camera path --------------------------------------------------------------
GRID = 48.0
CLEAR = 24.0
PATH_TARGET = 6000.0                        # units of path to aim for


def camera_path(bsp):
    """A tour through free space: a grid of points with CLEAR units of room
    around them, breadth-first routes from the player start to the nearest
    unvisited item, weapon or monster, string-pulled and smoothed."""
    from collections import deque
    free_cache = {}

    def free(p):
        k = (round(p[0], 1), round(p[1], 1), round(p[2], 1))
        v = free_cache.get(k)
        if v is None:
            v = free_cache[k] = bsp.contents(p) == CONTENTS_EMPTY
        return v

    def roomy(p):
        return free(p) and all(free((p[0] + dx, p[1] + dy, p[2] + dz)) for dx, dy, dz in
                               ((CLEAR, 0, 0), (-CLEAR, 0, 0), (0, CLEAR, 0), (0, -CLEAR, 0), (0, 0, CLEAR), (0, 0, -CLEAR)))

    def clear(a, b):
        d = math.dist(a, b)
        n = max(1, int(d / 8))
        return all(roomy(tuple(a[k] + (b[k] - a[k]) * i / n for k in range(3))) for i in range(n + 1))

    m = bsp.models[0]
    lo, hi = m[0:3], m[3:6]
    dims = [int((hi[k] - lo[k]) / GRID) + 1 for k in range(3)]

    def pt(c):
        return tuple(lo[k] + (c[k] + 0.5) * GRID for k in range(3))
    nodes = {}
    for i in range(dims[0]):
        for j in range(dims[1]):
            for k in range(dims[2]):
                p = pt((i, j, k))
                if roomy(p):
                    nodes[(i, j, k)] = p
    steps = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1),
             (1, 1, 0), (1, -1, 0), (-1, 1, 0), (-1, -1, 0)]
    edge_cache = {}

    def neighbours(c):
        for d in steps:
            n = (c[0] + d[0], c[1] + d[1], c[2] + d[2])
            if n in nodes:
                key = (c, n) if c < n else (n, c)
                ok = edge_cache.get(key)
                if ok is None:
                    ok = edge_cache[key] = clear(nodes[c], nodes[n])
                if ok:
                    yield n

    def nearest_node(p):
        c = tuple(int((p[k] - lo[k]) / GRID) for k in range(3))
        best = None
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                for dk in (-1, 0, 1, 2):
                    n = (c[0] + di, c[1] + dj, c[2] + dk)
                    if n in nodes and bsp.clear_line(p, nodes[n], 4.0):
                        d = math.dist(p, nodes[n])
                        if best is None or d < best[0]:
                            best = (d, n)
        return best[1] if best else None

    def bfs(src):
        prev, q = {src: None}, deque([src])
        while q:
            c = q.popleft()
            for n in neighbours(c):
                if n not in prev:
                    prev[n] = c
                    q.append(n)
        return prev

    start = next((e for e in bsp.entities if e.get("classname") == "info_player_start"), None)
    o = tuple(float(v) for v in start["origin"].split())
    cur = nearest_node((o[0], o[1], o[2] + EYE))
    targets = []
    for e in bsp.entities:
        if "origin" in e and e.get("classname", "").startswith(("item_", "weapon_", "monster_")):
            o = tuple(float(v) for v in e["origin"].split())
            n = nearest_node((o[0], o[1], o[2] + EYE))
            if n and n not in targets:
                targets.append(n)
    route, length, visited = [cur], 0.0, 0
    while targets and length < PATH_TARGET:
        prev = bfs(cur)
        reach = [t for t in targets if t in prev and t != cur]
        if not reach:
            break

        def hops(t):
            n = 0
            while t is not None:
                t, n = prev[t], n + 1
            return n
        nxt = min(reach, key=hops)
        chain = []
        t = nxt
        while t is not None and t != cur:
            chain.append(t)
            t = prev[t]
        chain.reverse()
        for c in chain:
            length += math.dist(nodes[route[-1]], nodes[c])
            route.append(c)
        targets = [t for t in targets if math.dist(nodes[t], nodes[nxt]) > GRID * 1.5]
        cur = nxt
        visited += 1
    pts = [nodes[c] for c in route]
    # String-pull: jump to the farthest point still in clear view.
    pulled, i = [pts[0]], 0
    while i < len(pts) - 1:
        j = min(len(pts) - 1, i + 24)
        while j > i + 1 and not clear(pts[i], pts[j]):
            j -= 1
        pulled.append(pts[j])
        i = j
    path = pulled
    # Dense Catmull-Rom, then resample at STEP * 8 units.
    dense = []
    n = len(path)
    for i in range(n - 1):
        p0, p1, p2, p3 = path[max(i - 1, 0)], path[i], path[i + 1], path[min(i + 2, n - 1)]
        for k in range(16):
            t = k / 16
            dense.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t +
                                      (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3) for j in range(3)))
    dense.append(path[-1])
    spacing = STEP * 8
    keys, acc = [dense[0]], 0.0
    for a, b in zip(dense, dense[1:]):
        acc += math.dist(a, b)
        if acc >= spacing:
            keys.append(b)
            acc = 0.0
    if math.dist(keys[-1], dense[-1]) > 1:
        keys.append(dense[-1])
    length = sum(math.dist(a, b) for a, b in zip(dense, dense[1:]))
    looks = []
    for i in range(len(keys)):
        j = min(i + 3, len(keys) - 1)
        if j == i:
            d = [keys[i][k] - keys[i - 1][k] for k in range(3)]
            looks.append(tuple(keys[i][k] + d[k] for k in range(3)))
        else:
            looks.append(keys[j])
    frames = int(min(MAX_FRAMES, max(120, length / STEP)))
    return [(q2y(p), q2y(l)) for p, l in zip(keys, looks)], frames, visited + 1, length


# ---- conversion -------------------------------------------------------------------
SKIP = ("clip", "trigger", "skip", "hint", "origin", "null", "nodraw")


def convert(args):
    import assets
    data, palette = load_from_lite(assets.src(LITE), args["map"])
    bsp = Bsp(data)
    model = bsp.models[0]
    first, count = model[14], model[15]
    # Textures used by the world, at their original sizes.
    used = {}
    faces = []
    for fi in range(first, first + count):
        f = bsp.faces[fi]
        ti = bsp.texinfo[f[4]]
        mt = bsp.miptex[ti[8]]
        if not mt or mt[0].startswith(SKIP):
            continue
        faces.append(fi)
        used.setdefault(ti[8], mt)
    tex_img = {}
    for mi, (name, w, h, pix) in used.items():
        rgba = miptex_rgba(pix, palette, name.startswith("{"))
        if name.startswith("sky") and w == 2 * h:
            half = w // 2                       # the back layer: the right half
            rgba = b"".join(rgba[(y * w + half) * 4:(y * w + w) * 4] for y in range(h))
            w = half
        tw, th, trgba = dbs.fit_texture(w, h, rgba, 256)
        tex_img[mi] = [name, w, h, tw, th, trgba]
    # Lightmaps.
    atlas = Atlas()
    lm_of = {}
    for fi in faces:
        f = bsp.faces[fi]
        name = bsp.miptex[bsp.texinfo[f[4]][8]][0]
        if f[9] < 0 or f[5] == 255 or name.startswith(("*", "sky")):
            continue
        mins, size = face_extents(bsp, f)
        if size[0] > LM_PAGE or size[1] > LM_PAGE:
            continue
        page, (x, y) = atlas.alloc(size[0], size[1])
        texels = atlas.pages[page][1]
        for t in range(size[1]):
            for s in range(size[0]):
                v = bsp.light[f[9] + t * size[0] + s]
                texels[(y + t) * LM_PAGE + x + s] = min(255, int(v * LM_BOOST))
        lm_of[fi] = (page, x, y, mins, size)
    # Budget: halve the largest world textures until everything fits.
    lm_bytes = len(atlas.pages) * LM_PAGE * LM_PAGE

    def tex_bytes():
        return sum(t[3] * t[4] * 2 * 4 // 3 for t in tex_img.values()) + lm_bytes + 32 * 64 * 2
    halvings = 0
    while tex_bytes() > BUDGET:
        big = max(tex_img.values(), key=lambda t: t[3] * t[4])
        if big[3] <= 16 and big[4] <= 16:
            break
        nw, nh = max(1, big[3] // 2), max(1, big[4] // 2)
        big[5] = dbs.resize_box(big[3], big[4], big[5], nw, nh)
        big[3], big[4] = nw, nh
        halvings += 1
    s = dbs.Scene()
    tex_index = {mi: s.add_texture(t[3], t[4], t[5], dbs.TF_MIPMAP) for mi, t in tex_img.items()}
    lm_index = []
    for heights, texels in atlas.pages:
        rgba = bytearray(LM_PAGE * LM_PAGE * 4)
        rgba[0::4] = rgba[1::4] = rgba[2::4] = texels
        rgba[3::4] = b"\xff" * (LM_PAGE * LM_PAGE)
        lm_index.append(s.add_texture(LM_PAGE, LM_PAGE, bytes(rgba), dbs.TF_LIGHTMAP | dbs.TF_CLAMP))
    fw, fh, frgba = flame_texture()
    flame = s.add_texture(fw, fh, frgba, 0)
    # Faces into batches keyed by (texture, lightmap page, flags).
    groups = {}
    for fi in faces:
        f = bsp.faces[fi]
        ti = bsp.texinfo[f[4]]
        name, ow, oh = tex_img[ti[8]][0], tex_img[ti[8]][1], tex_img[ti[8]][2]
        flags = 0
        alpha = 255
        if name.startswith("sky"):
            flags = dbs.BF_SKY
        elif name.startswith("*"):
            if not name.startswith(("*lava", "*tele")):
                flags, alpha = dbs.BF_TRANS, 150
        elif name.startswith("{"):
            flags = dbs.BF_ALPHATEST
        lm = lm_of.get(fi)
        key = (tex_index[ti[8]], lm_index[lm[0]] if lm else dbs.NONE, flags)
        verts = []
        for p in bsp.face_points(f):
            sc = p[0] * ti[0] + p[1] * ti[1] + p[2] * ti[2] + ti[3]
            tc = p[0] * ti[4] + p[1] * ti[5] + p[2] * ti[6] + ti[7]
            u, v = sc / ow, tc / oh
            if lm:
                page, x, y, mins, size = lm
                ls, lt = (sc - mins[0]) / 16.0, (tc - mins[1]) / 16.0
                u2, v2 = (x + ls + 0.5) / LM_PAGE, (y + lt + 0.5) / LM_PAGE
                lv = min(255, int(sample(bsp.light, f[9], size[0], size[1], ls, lt) * LM_BOOST))
            else:
                u2 = v2 = 0.0
                lv = 255
            q = q2y(p)
            verts.append((q[0], q[1], q[2], lv, lv, lv, alpha, u, v, u2, v2))
        # Quake's faces wind clockwise from the front: reverse for counter-clockwise.
        verts.reverse()
        g = groups.setdefault(key, [])
        g.append((fi, verts))
    face_rec = {}                                  # bsp face -> (batch, ifirst, icount)
    for key, flist in sorted(groups.items()):
        cur_v, cur_t, members = [], [], []
        def flush():
            if cur_t:
                s.add_batch(cur_v[:], [tuple(t) for t in cur_t], tex=key[0], lm=key[1], flags=key[2])
                b = len(s.batches) - 1
                base = s.batches[b]["ifirst"]
                for fi, off, n in members:
                    face_rec[fi] = (b, base + off, n)
        for fi, verts in flist:
            if len(cur_v) + len(verts) > 65536:
                flush()
                cur_v, cur_t, members = [], [], []
            b0 = len(cur_v)
            cur_v.extend(verts)
            start = len(cur_t) * 3
            for k in range(1, len(verts) - 1):
                cur_t.append((b0, b0 + k, b0 + k + 1))
            members.append((fi, start, (len(verts) - 2) * 3))
        flush()
    # Sprites: one vertex per torch or flame (the runtime makes billboards).
    sprites = []
    for e in bsp.entities:
        cn = e.get("classname", "")
        if ("torch" in cn or "flame" in cn or cn == "light") and "origin" in e:
            o = [float(v) for v in e["origin"].split()]
            if bsp.contents(o) != CONTENTS_EMPTY:
                continue
            size = 24.0 if "large" in cn else 16.0
            q = q2y((o[0], o[1], o[2] + (4 if "torch" in cn else 0)))
            sprites.append((q[0], q[1], q[2], 255, 255, 255, 255, size, size * 2, 0.0, 0.0))
    if sprites:
        s.add_batch(sprites, [], tex=flame, flags=dbs.BF_SPRITE | dbs.BF_ALPHATEST)
    # Visibility: nodes, leaves, marks, faces, PVS.
    order = {fi: k for k, fi in enumerate(sorted(face_rec, key=lambda fi: face_rec[fi]))}
    vis = bytearray()
    nleaves = len(bsp.leaves)
    marks_out = []
    leaf_out = []
    for leaf in bsp.leaves:
        first_mark = len(marks_out)
        for m in range(leaf[8], leaf[8] + leaf[9]):
            fi = bsp.marks[m][0]
            if fi in order:
                marks_out.append(order[fi])
        lo = q2y((leaf[2], leaf[3], leaf[4]))
        hi = q2y((leaf[5], leaf[6], leaf[7]))
        mins = tuple(min(a, b) for a, b in zip(lo, hi))
        maxs = tuple(max(a, b) for a, b in zip(lo, hi))
        leaf_out.append(struct.pack("<2i6f2I", leaf[0], leaf[1], *mins, *maxs, first_mark, len(marks_out) - first_mark))
    node_out = []
    for n in bsp.nodes:
        pl = bsp.planes[n[0]]
        nrm = q2y(pl[:3])
        node_out.append(struct.pack("<4f2i", nrm[0], nrm[1], nrm[2], pl[3], n[1], n[2]))
    face_out = [struct.pack("<2HI", face_rec[fi][0], face_rec[fi][2], face_rec[fi][1])
                for fi in sorted(order, key=lambda fi: order[fi])]
    head = struct.pack("<8I", len(node_out), nleaves, len(marks_out), len(face_out), len(bsp.vis),
                       model[13], model[9], 0)
    vis += head + b"".join(node_out) + b"".join(leaf_out) + struct.pack("<%dI" % len(marks_out), *marks_out)
    vis += b"".join(face_out) + bsp.vis
    s.vis = bytes(vis) + b"\0" * (-len(vis) % 4)
    keys, frames, npoints, length = camera_path(bsp)
    s.view = dict(kind=1, frames=frames, znear=4.0, zfar=4096.0, keys=keys)
    s.info.update(map=args["map"], faces=str(len(faces)), lightmap_pages=str(len(atlas.pages)),
                  texture_halvings=str(halvings), tex16_bytes=str(tex_bytes()), path_points=str(npoints),
                  path_length=str(int(length)), sprites=str(len(sprites)))
    print("bsp: %s: %d faces, %d textures (%d halvings), %d lightmap pages, %d leaves, %d marks, "
          "path %d points %.0f units %d frames, %d sprites, %d B texture memory" %
          (args["map"], len(faces), len(tex_img), halvings, len(atlas.pages), nleaves, len(marks_out), npoints,
           length, frames, len(sprites), tex_bytes()))
    return s
