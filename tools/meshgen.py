"""Meshes for the game-scene generators: primitives with normals, texture
coordinates and colours, and the operations to build models from them.
Counter-clockwise triangles face outwards. Models face +z with +y up
(src/core/vmath.h). Standard library only.
"""
import math

from gsgen import add, cross, mul, norm, sub


class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.col, self.tris = [], [], [], [], []
        self.uv2 = None

    def vert(self, p, n, uv, col=(255, 255, 255, 255)):
        self.pos.append(tuple(p))
        self.nrm.append(tuple(n))
        self.uv.append(tuple(uv))
        self.col.append(tuple(col) if len(col) == 4 else tuple(col) + (255,))
        return len(self.pos) - 1

    def tri(self, a, b, c):
        self.tris.append((a, b, c))

    def quad(self, a, b, c, d):
        self.tris += [(a, b, c), (a, c, d)]

    def merge(self, other):
        base = len(self.pos)
        self.pos += other.pos
        self.nrm += other.nrm
        self.uv += other.uv
        self.col += other.col
        self.tris += [(a + base, b + base, c + base) for a, b, c in other.tris]
        if other.uv2 is not None or self.uv2 is not None:
            mine = self.uv2 or [(0.0, 0.0)] * base
            self.uv2 = mine + (other.uv2 or [(0.0, 0.0)] * len(other.pos))
        return self

    def colour(self, rgba):
        rgba = tuple(rgba) if len(rgba) == 4 else tuple(rgba) + (255,)
        self.col = [rgba] * len(self.pos)
        return self

    def transform(self, scale=(1, 1, 1), rot=(0, 0, 0), move=(0, 0, 0)):
        """Scale, then rotate yaw (y), pitch (x), roll (z) in degrees as vmath's m4_ypr, then move."""
        def rotate(v):
            x, y, z = v
            r = math.radians(rot[2])
            x, y = x * math.cos(r) - y * math.sin(r), x * math.sin(r) + y * math.cos(r)
            r = math.radians(rot[1])
            y, z = y * math.cos(r) - z * math.sin(r), y * math.sin(r) + z * math.cos(r)
            r = math.radians(rot[0])
            x, z = x * math.cos(r) + z * math.sin(r), -x * math.sin(r) + z * math.cos(r)
            return (x, y, z)
        inv = (1.0 / scale[0], 1.0 / scale[1], 1.0 / scale[2])
        self.pos = [add(rotate((p[0] * scale[0], p[1] * scale[1], p[2] * scale[2])), move) for p in self.pos]
        self.nrm = [norm(rotate((n[0] * inv[0], n[1] * inv[1], n[2] * inv[2]))) for n in self.nrm]
        if scale[0] * scale[1] * scale[2] < 0:
            self.tris = [(a, c, b) for a, b, c in self.tris]
        return self

    def map_uv(self, fn):
        self.uv = [fn(p, uv) for p, uv in zip(self.pos, self.uv)]
        return self

    def copy(self):
        m = Mesh()
        m.pos, m.nrm, m.uv, m.col, m.tris = list(self.pos), list(self.nrm), list(self.uv), list(self.col), list(self.tris)
        m.uv2 = list(self.uv2) if self.uv2 is not None else None
        return m

    def ntris(self):
        return len(self.tris)


def box(sx, sy, sz, uvs=1.0, col=(255, 255, 255)):
    """A box of these sizes centred on the origin; each face its own vertices (flat
    normals), texture coordinates in units of uvs metres (or 0..1 per face if uvs is 0)."""
    m = Mesh()
    h = (sx / 2.0, sy / 2.0, sz / 2.0)
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    for n, a, b in faces:
        ia = [i for i in range(3) if a[i]][0]
        ib = [i for i in range(3) if b[i]][0]
        idx = []
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = tuple(h[k] * (n[k] + su * a[k] + sv * b[k]) for k in range(3))
            if uvs:
                uv = (su * h[ia] / uvs, sv * h[ib] / uvs)
            else:
                uv = ((su + 1) / 2.0, (sv + 1) / 2.0)
            idx.append(m.vert(p, n, uv, col))
        m.quad(*idx)
    return m


def cylinder(r, h, n=12, caps=True, uvs=1.0, col=(255, 255, 255), r2=None):
    """Along y, from -h/2 to h/2; r2: the top radius (a cone or frustum)."""
    m = Mesh()
    r2 = r if r2 is None else r2
    slope = (r - r2) / h
    for i in range(n + 1):
        a = 2 * math.pi * i / n
        c, s = math.cos(a), math.sin(a)
        nn = norm((c, slope, s))
        u = (2 * math.pi * r * i / n) / uvs if uvs else i / float(n)
        m.vert((r * c, -h / 2.0, r * s), nn, (u, 0), col)
        m.vert((r2 * c, h / 2.0, r2 * s), nn, (u, h / uvs if uvs else 1), col)
    for i in range(n):
        a, b, c_, d = 2 * i, 2 * i + 1, 2 * i + 3, 2 * i + 2
        if r2 > 0:
            m.quad(a, b, c_, d)
        else:
            m.tri(a, c_, d)                         # a cone: no triangle at the apex
    if caps:
        for y, rr, ny in ((-h / 2.0, r, -1), (h / 2.0, r2, 1)):
            if rr <= 0:
                continue
            c0 = m.vert((0, y, 0), (0, ny, 0), (0.5, 0.5), col)
            ring = [m.vert((rr * math.cos(2 * math.pi * i / n), y, rr * math.sin(2 * math.pi * i / n)), (0, ny, 0),
                           (0.5 + 0.5 * math.cos(2 * math.pi * i / n), 0.5 + 0.5 * math.sin(2 * math.pi * i / n)), col)
                    for i in range(n)]
            for i in range(n):
                if ny > 0:
                    m.tri(c0, ring[(i + 1) % n], ring[i])
                else:
                    m.tri(c0, ring[i], ring[(i + 1) % n])
    return m


def sphere(r, nu=16, nv=8, col=(255, 255, 255)):
    """A UV sphere: u round the y axis, v from the bottom pole to the top."""
    m = Mesh()
    for j in range(nv + 1):
        th = math.pi * j / nv
        for i in range(nu + 1):
            ph = 2 * math.pi * i / nu
            n = (math.sin(th) * math.cos(ph), -math.cos(th), math.sin(th) * math.sin(ph))
            m.vert(mul(n, r), n, (i / float(nu), j / float(nv)), col)
    w = nu + 1
    for j in range(nv):
        for i in range(nu):
            a, b = j * w + i, j * w + i + 1
            c, d = (j + 1) * w + i + 1, (j + 1) * w + i
            if j > 0:
                m.tri(a, c, b)
            if j < nv - 1:
                m.tri(a, d, c)
    return m


def icosphere(r, subdiv=1, col=(255, 255, 255)):
    """A geodesic sphere (smooth normals), for rocks: 20 * 4^subdiv triangles."""
    t = (1 + math.sqrt(5)) / 2
    vs = [norm(v) for v in ((-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t),
                            (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1))]
    fs = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
          (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
          (8, 6, 7), (9, 8, 1)]
    for _ in range(subdiv):
        mid = {}

        def m_(a, b):
            k = (min(a, b), max(a, b))
            if k not in mid:
                vs.append(norm(add(vs[a], vs[b])))
                mid[k] = len(vs) - 1
            return mid[k]
        nf = []
        for a, b, c in fs:
            ab, bc, ca = m_(a, b), m_(b, c), m_(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        fs = nf
    m = Mesh()
    for v in vs:
        m.vert(mul(v, r), v, (0.5 + math.atan2(v[2], v[0]) / (2 * math.pi), 0.5 - math.asin(v[1]) / math.pi), col)
    m.tris = list(fs)                               # counter-clockwise from outside
    return m


def displace(m, fn):
    """Move each vertex along its normal by fn(position), then recompute smooth normals."""
    m.pos = [add(p, mul(n, fn(p))) for p, n in zip(m.pos, m.nrm)]
    return smooth_normals(m)


def smooth_normals(m):
    acc = [(0.0, 0.0, 0.0)] * len(m.pos)
    for a, b, c in m.tris:
        n = cross(sub(m.pos[b], m.pos[a]), sub(m.pos[c], m.pos[a]))
        for i in (a, b, c):
            acc[i] = add(acc[i], n)
    m.nrm = [norm(n) for n in acc]
    return m


def torus(R, r, nu=24, nv=8, uvs=1.0, col=(255, 255, 255)):
    """In the x-z plane round the y axis. The texture repeats a whole number of times
    each way (about every uvs metres, at most 32: the coordinate limit), so it meets itself."""
    m = Mesh()
    ru = max(1, min(32, int(round(2 * math.pi * R / uvs))))
    rv = max(1, min(32, int(round(2 * math.pi * r / uvs))))
    for i in range(nu + 1):
        a = 2 * math.pi * i / nu
        for j in range(nv + 1):
            b = 2 * math.pi * j / nv
            n = (math.cos(b) * math.cos(a), math.sin(b), math.cos(b) * math.sin(a))
            p = ((R + r * math.cos(b)) * math.cos(a), r * math.sin(b), (R + r * math.cos(b)) * math.sin(a))
            m.vert(p, n, (ru * i / float(nu), rv * j / float(nv)), col)
    w = nv + 1
    for i in range(nu):
        for j in range(nv):
            a, b, c, d = i * w + j, (i + 1) * w + j, (i + 1) * w + j + 1, i * w + j + 1
            m.quad(a, d, c, b)
    return m


def quad(w, h, col=(255, 255, 255, 255), uv=((0, 0), (1, 1))):
    """In the x-y plane facing +z."""
    m = Mesh()
    (u0, v0), (u1, v1) = uv
    i = [m.vert((-w / 2.0, -h / 2.0, 0), (0, 0, 1), (u0, v0), col), m.vert((w / 2.0, -h / 2.0, 0), (0, 0, 1), (u1, v0), col),
         m.vert((w / 2.0, h / 2.0, 0), (0, 0, 1), (u1, v1), col), m.vert((-w / 2.0, h / 2.0, 0), (0, 0, 1), (u0, v1), col)]
    m.quad(*i)
    return m


def crossed(w, l, col=(255, 255, 255, 255)):
    """Two quads crossing along z (a laser bolt, a flame): w wide, l long; draw two-sided."""
    a = quad(w, l, col).transform(rot=(0, 90, 0))           # in x-z
    b = quad(w, l, col).transform(rot=(0, 90, 90))          # in y-z
    return a.merge(b)


def wedge(w, h, l, col=(255, 255, 255)):
    """A nose: a box's back face narrowing to a front edge (along +z), flat normals."""
    m = Mesh()
    hw, hh, hl = w / 2.0, h / 2.0, l / 2.0
    P = [(-hw, -hh, -hl), (hw, -hh, -hl), (hw, hh, -hl), (-hw, hh, -hl), (-hw * 0.2, -hh * 0.4, hl), (hw * 0.2, -hh * 0.4, hl)]
    faces = [(0, 3, 2, 1), (0, 1, 5, 4), (3, 4, 5, 2), (0, 4, 3), (1, 2, 5)]
    for f in faces:
        n = norm(cross(sub(P[f[1]], P[f[0]]), sub(P[f[2]], P[f[0]])))
        idx = [m.vert(P[k], n, (P[k][0] / 4.0 + P[k][2] / 8.0, P[k][1] / 4.0 + P[k][2] / 8.0), col) for k in f]
        if len(idx) == 4:
            m.quad(*idx)
        else:
            m.tri(*idx)
    return m
