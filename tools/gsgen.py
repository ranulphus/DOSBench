"""Shared pieces of the game-scene generators (tools/scene_*.py).

PCG32 random numbers (the same content from the same seed on every Python),
vector helpers, meshes into a dbs.Scene (lighting baked into vertex colours,
or normals kept for the runtime's CPU lighting), tracks resampled to evenly
spaced keys, and the runtime's track evaluation (src/core/gs.c), so a
generator knows where things will be. Standard library only.
"""
import math

import dbs

M64 = (1 << 64) - 1


class PCG32:
    """PCG-XSH-RR, 64-bit state: small, fast enough, and fixed forever."""

    def __init__(self, seed, seq=54):
        self.state = 0
        self.inc = ((seq << 1) | 1) & M64
        self.next()
        self.state = (self.state + seed) & M64
        self.next()

    def next(self):
        old = self.state
        self.state = (old * 6364136223846793005 + self.inc) & M64
        xs = (((old >> 18) ^ old) >> 27) & 0xFFFFFFFF
        rot = old >> 59
        return ((xs >> rot) | (xs << ((-rot) & 31))) & 0xFFFFFFFF

    def random(self):
        return self.next() / 4294967296.0

    def uniform(self, a, b):
        return a + (b - a) * self.random()

    def randint(self, a, b):
        return a + self.next() % (b - a + 1)

    def choice(self, seq):
        return seq[self.next() % len(seq)]

    def unit(self):
        """A random unit vector."""
        z = self.uniform(-1, 1)
        a = self.uniform(0, 2 * math.pi)
        r = math.sqrt(1 - z * z)
        return (r * math.cos(a), r * math.sin(a), z)


# ---- Vectors (tuples) ---------------------------------------------------------
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def lerp(a, b, t): return tuple(x + (y - x) * t for x, y in zip(a, b))


def norm(a):
    n = length(a)
    return (a[0] / n, a[1] / n, a[2] / n) if n > 0 else (0.0, 1.0, 0.0)


def clamp(x, lo=0.0, hi=1.0):
    return lo if x < lo else hi if x > hi else x


# ---- Meshes into a scene ----------------------------------------------------------
def shade(n, rgb, sun, sun_rgb, ambient, wrap=0.0):
    """A colour lit by a sun and ambient light (wrap > 0 softens the terminator)."""
    d = max(0.0, (dot(n, sun) + wrap) / (1 + wrap))
    return tuple(clamp(c / 255.0 * (ambient[k] + sun_rgb[k] * d)) * 255 for k, c in enumerate(rgb))


def add_mesh(s, mesh, tex=dbs.NONE, flags=0, lit=False, light=None, alpha=None):
    """Mesh (tools/meshgen.py) as one batch. lit: keep normals for the runtime's
    CPU lighting (the model must be MF_LIT). light = (sun, sun_rgb, ambient[, wrap]):
    bake it into the vertex colours. Returns the batch index."""
    verts = []
    for i, p in enumerate(mesh.pos):
        c = mesh.col[i]
        rgb = c[:3]
        if light and not lit:
            rgb = shade(mesh.nrm[i], rgb, *light)
        a = c[3] if alpha is None else alpha
        u, v = mesh.uv[i]
        u2, v2 = mesh.uv2[i] if mesh.uv2 else (0.0, 0.0)
        verts.append((p[0], p[1], p[2], rgb[0], rgb[1], rgb[2], a, u, v, u2, v2))
    vfirst = len(s.verts)
    s.add_batch(verts, mesh.tris, tex=tex, flags=flags)
    if lit:
        s.set_normals(vfirst, mesh.nrm)
    return len(s.batches) - 1


def add_model(s, parts, lit=False, light=None, flags=0, lod_next=dbs.NONE, lod_dist=0.0):
    """A model from parts [(mesh, tex, batch flags[, light]), ...], added as consecutive
    batches; a part's own light (None: unshaded) replaces the model's."""
    b0 = len(s.batches)
    for part in parts:
        mesh, tex, bf = part[:3]
        add_mesh(s, mesh, tex, bf, lit=lit, light=part[3] if len(part) > 3 else light)
    return s.add_model(b0, len(parts), flags=flags | (dbs.MF_LIT if lit else 0), lod_next=lod_next,
                       lod_dist=lod_dist)


def model_tris(s, m):
    md = s.models[m]
    return sum(b["icount"] // 3 for b in s.batches[md["batch0"]:md["batch0"] + md["nbatch"]])


# ---- Tracks ------------------------------------------------------------------------
def catmull(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return tuple(0.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 +
                        (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in range(len(p1)))


def track(points, closed=False, spacing=8.0, rolls=None):
    """A TRAK record: a Catmull-Rom curve through points (x, y, z), resampled to
    keys evenly spaced along it (the runtime's spacing is length / keys). rolls:
    degrees per point (+ leans left), or None for 0."""
    n = len(points)
    rolls = rolls or [0.0] * n
    pts = [tuple(p) + (r,) for p, r in zip(points, rolls)]
    segs = n if closed else n - 1
    dense = []
    for i in range(segs):
        if closed:
            q = [pts[(i + k - 1) % n] for k in range(4)]
        else:
            q = [pts[max(0, i - 1)], pts[i], pts[i + 1], pts[min(n - 1, i + 2)]]
        for j in range(64):
            dense.append(catmull(q[0], q[1], q[2], q[3], j / 64.0))
    if not closed:
        dense.append(pts[-1])
    else:
        dense.append(dense[0])
    acc = [0.0]
    for a, b in zip(dense, dense[1:]):
        acc.append(acc[-1] + length(sub(b[:3], a[:3])))
    total = acc[-1]
    nkeys = max(2 if not closed else 4, int(round(total / spacing)) + (0 if closed else 1))
    step = total / (nkeys if closed else nkeys - 1)
    keys, j = [], 0
    for k in range(nkeys):
        d = k * step
        while j < len(acc) - 2 and acc[j + 1] < d:
            j += 1
        f = (d - acc[j]) / max(1e-9, acc[j + 1] - acc[j])
        keys.append(lerp(dense[j], dense[j + 1], clamp(f)))
    return dict(keys=keys, closed=closed, length=total)


def track_at(tr, d):
    """Position, forward and up on a track at distance d: src/core/gs.c's gs_track_at."""
    keys, n, closed = tr["keys"], len(tr["keys"]), tr["closed"]
    spacing = tr["length"] / (n if closed else n - 1)
    if closed:
        u = (d / spacing) % n
    else:
        u = clamp(d / spacing, 0, n - 1)
    k = int(u)
    if not closed:
        k = min(k, n - 2)
    t = u - k
    if closed:
        i = [(k - 1) % n, k, (k + 1) % n, (k + 2) % n]
    else:
        i = [max(0, k - 1), k, k + 1, min(n - 1, k + 2)]
    p = [keys[x] for x in i]
    pos = catmull(p[0][:3], p[1][:3], p[2][:3], p[3][:3], t)
    a = catmull(p[0][:3], p[1][:3], p[2][:3], p[3][:3], min(1.0, t + 1e-3))
    b = catmull(p[0][:3], p[1][:3], p[2][:3], p[3][:3], max(0.0, t - 1e-3))
    fwd = norm(sub(a, b))
    roll = math.radians(p[1][3] + (p[2][3] - p[1][3]) * t)
    left = cross((0, 1, 0), fwd)
    left = norm(left) if length(left) > 1e-6 else (1.0, 0.0, 0.0)
    y = cross(fwd, left)
    up = add(mul(y, math.cos(roll)), mul(left, math.sin(roll)))
    return pos, fwd, up


def track_point(tr, d, lateral=0.0, height=0.0):
    """Where a TRACK instance with these offsets is (gs.c's inst_local)."""
    pos, fwd, up = track_at(tr, d)
    left = norm(cross(up, fwd))
    return add(pos, add(mul(left, lateral), mul(up, height)))


def budget(s, name, tris=None):
    """Print what the scene holds: geometry, models, instances, textures at 16 bits."""
    print("%s: %d triangles in %d batches, %d models, %d instances, %d particle kinds, %d emitters, %d bursts, "
          "%d shots, textures %d KB" % (name, s.triangles(), len(s.batches), len(s.models), len(s.instances),
                                        len(s.parts), len(s.emitters), len(s.bursts), len(s.shots),
                                        s.texture_bytes16() // 1024))
