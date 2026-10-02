"""G2RACE Canyon race (CANYON.DBS): procedural, 40 seconds at 25 frames/s.

Six buggies race round a 2.4 km banked dirt loop at the bottom of a canyon:
walls of striped sandstone, natural arches over the road, boulders and
cacti, a sky dome with the sun low and haze from 300 m. Each buggy throws
dust (blended particles) and backfires now and then; its wheels spin. The
canyon is cut into chunks, so the frustum and the haze keep only the near
ones. What it asks of a card: a lot of geometry, fog, and blended dust
close to the camera (docs/tests.md). PCG32 seeds: the same file everywhere.
Standard library only.
"""
import math

import dbs
import gsgen as G
import meshgen as M
import texgen as T

RATE = 25.0
FRAMES = 1000
SUN = G.norm((-0.55, 0.55, 0.35))
SUN_RGB = (1.1, 0.98, 0.82)
AMBIENT = (0.42, 0.40, 0.45)
HORIZON = (214, 196, 178)
ROAD_W = 7.0                                    # half the road's width
STEP = 6.0                                      # metres between the canyon's cross-sections
CHUNKS = 24
LIGHT = (SUN, SUN_RGB, AMBIENT)


def smooth1(x, seed):
    """Smooth 1D noise in [-1, 1], periodic every 64 units."""
    i = int(math.floor(x))
    f = x - i
    f = f * f * (3 - 2 * f)
    a = T.hash3(i % 64, 0, seed) / 2147483647.5 - 1
    b = T.hash3((i + 1) % 64, 0, seed) / 2147483647.5 - 1
    return a + (b - a) * f


def profile(side, u, seed):
    """The canyon's cross-section on one side: (outward offset from the road's edge, height), road edge first."""
    n = [smooth1(u * 64 + k * 17, seed + side * 11 + k) for k in range(5)]
    return [(0.0, 0.0), (3.0, 0.15), (5.5 + n[0], 1.2 + 0.5 * n[1]), (8.0 + 2 * n[0], 10 + 5 * n[2]),
            (12.0 + 3 * n[1], 26 + 9 * n[3]), (20.0 + 5 * n[2], 40 + 12 * n[4]), (55.0, 46 + 14 * n[3])]


def sandstone(size, seed):
    """Striped red sandstone, the stripes across v."""
    def fn(u, v, x, y):
        n = T.fbm(u, v, seed, base=4, octaves=4)
        band = 0.5 + 0.5 * math.sin((v + 0.08 * n) * 2 * math.pi * 5)
        base = T.mix((150, 82, 52), (205, 140, 95), band)
        k = 0.8 + 0.35 * n
        return (base[0] * k, base[1] * k, base[2] * k, 255)
    return T.image(size, size, fn)


def dirt(size, seed):
    """A dirt road, across u, with two darker wheel ruts."""
    def fn(u, v, x, y):
        n = T.fbm(u, v, seed, base=8, octaves=4)
        rut = 0.82 if 0.22 < u < 0.32 or 0.68 < u < 0.78 else 1.0
        k = (0.75 + 0.4 * n) * rut
        return (176 * k, 140 * k, 102 * k, 255)
    return T.image(size, size, fn)


def cactus_tex(size):
    """Alpha-free green with vertical ribs."""
    def fn(u, v, x, y):
        rib = 0.75 + 0.25 * abs(math.sin(u * math.pi * 8))
        return (70 * rib, 120 * rib, 60 * rib, 255)
    return T.image(size, size, fn)


def tyre(size):
    def fn(u, v, x, y):
        r = math.hypot(u - 0.5, v - 0.5) * 2
        if r < 0.55:
            spoke = 0.6 + 0.4 * (abs(math.sin(math.atan2(v - 0.5, u - 0.5) * 3)) > 0.5)
            return (170 * spoke, 170 * spoke, 175 * spoke, 255)
        tread = 0.7 + 0.3 * (int(math.atan2(v - 0.5, u - 0.5) * 12) % 2)
        return (40 * tread, 40 * tread, 42 * tread, 255)
    return T.image(size, size, fn)


def sky_dome():
    """A hemisphere of vertex colours: haze at the horizon, blue at the top."""
    m = M.sphere(600, 24, 12)
    for i, p in enumerate(m.pos):
        h = max(0.0, p[1] / 600.0)
        c = T.mix(HORIZON, (70, 120, 200), min(1.0, h * 2.2) ** 0.8)
        m.col[i] = (c[0], c[1], c[2], 255)
    m.tris = [(a, c, b) for a, b, c in m.tris]       # seen from inside
    return m


def buggy(rgb):
    parts = [M.box(1.9, 0.5, 3.6, uvs=1.0).transform(move=(0, 0.55, 0)),
             M.wedge(1.9, 0.5, 1.0).transform(move=(0, 0.5, 2.2)),
             M.box(1.4, 0.7, 1.6, uvs=1.0).transform(move=(0, 1.15, -0.2)),
             M.box(1.7, 0.08, 0.5, uvs=1.0).transform(move=(0, 1.35, -1.9)),
             M.box(0.1, 0.5, 0.1, uvs=1.0).transform(move=(0.6, 1.05, -1.9)),
             M.box(0.1, 0.5, 0.1, uvs=1.0).transform(move=(-0.6, 1.05, -1.9))]
    for x in (-0.65, 0.65):
        parts.append(M.box(0.08, 0.08, 1.8, uvs=1.0).transform(move=(x, 1.55, -0.2)))
    m = M.Mesh()
    for p in parts:
        m.merge(p)
    return m.colour(rgb)


def saguaro(rng):
    m = M.cylinder(0.35, 4.2, 6, uvs=1.0).transform(move=(0, 2.1, 0))
    for side in (-1, 1):
        if rng.random() < 0.8:
            h = rng.uniform(1.6, 2.6)
            m.merge(M.cylinder(0.22, 0.9, 6, uvs=1.0).transform(rot=(0, 0, 90), move=(side * 0.7, h, 0)))
            m.merge(M.cylinder(0.22, 1.4, 6, uvs=1.0).transform(move=(side * 1.12, h + 0.6, 0)))
    return m.colour((255, 255, 255))


def build(args=None):
    rng = G.PCG32(2402)
    s = dbs.Scene()
    tex = s.add_texture
    t_rock = tex(256, 256, sandstone(256, 71))
    t_dirt = tex(256, 256, dirt(256, 72))
    t_cactus = tex(32, 32, cactus_tex(32))
    t_tyre = tex(64, 64, tyre(64), dbs.TF_CLAMP)
    t_body = tex(64, 64, T.plating(64, 73, (225, 225, 225), panels=2))
    t_dust = tex(64, 64, T.smoke(64, 74, 230), dbs.TF_CLAMP)
    t_fire = tex(64, 64, T.fire(64, 75), dbs.TF_CLAMP)
    t_sun = tex(64, 64, T.glow(64, (255, 235, 190), 1.4), dbs.TF_CLAMP)
    t_boulder = tex(64, 64, T.rock(64, 76, (160, 110, 80)))

    # ---- The track: a banked loop with hills ---------------------------------------------------------
    pts = []
    for i in range(14):
        a = 2 * math.pi * i / 14
        r = 380 * (1 + rng.uniform(-0.22, 0.22))
        pts.append((r * math.cos(a), 14 * math.sin(3 * a) + rng.uniform(-6, 6), 0.8 * r * math.sin(a)))
    tr = G.track(pts, closed=True, spacing=4.0)
    keys, n = tr["keys"], len(tr["keys"])
    banked = []
    for i in range(n):                              # bank into the turns, gently
        a, b, c = keys[i - 1][:3], keys[i][:3], keys[(i + 1) % n][:3]
        dh = (math.atan2(c[0] - b[0], c[2] - b[2]) - math.atan2(b[0] - a[0], b[2] - a[2]) + math.pi) % (2 * math.pi) - math.pi
        banked.append(b + (max(-12.0, min(12.0, math.degrees(dh) * 6.0)),))
    tr["keys"] = banked
    s.tracks.append(tr)
    length = tr["length"]

    # ---- The canyon, in chunks ----------------------------------------------------------------------
    rings = int(round(length / STEP))
    rings -= rings % CHUNKS
    per = rings // CHUNKS
    road_rep = max(1, int(round(per * STEP / 8.0)))         # dirt every 8 m, whole repeats per chunk
    m_chunks = []
    frames = []
    for i in range(rings + 1):
        d = length * i / rings
        pos, fwd, up = G.track_at(tr, d)
        left = G.norm(G.cross(up, fwd))
        frames.append((pos, left, up, i / rings))
    for c in range(CHUNKS):
        road, walls = M.Mesh(), M.Mesh()
        r0 = c * per
        for side, sgn in ((0, 1.0), (1, -1.0)):
            prof = None
            idx_rows = []
            for k in range(r0, r0 + per + 1):
                pos, left, up, u = frames[k]
                prof = profile(side, u, 900)
                v = road_rep * (k - r0) / float(per)
                row = []
                for j, (off, h) in enumerate(prof):
                    lat = sgn * (ROAD_W + off)
                    up_k = up if j < 3 else (0, 1, 0)       # the walls stand straight, the shoulders bank
                    p = G.add(G.add(pos, G.mul(left, lat)), G.mul(up_k, h))
                    tv = (j * 0.25 + h / 18.0)
                    row.append(walls.vert(p, (0, 1, 0), (v * 0.5, tv), (255, 255, 255)))
                idx_rows.append(row)
            for a_, b_ in zip(idx_rows, idx_rows[1:]):
                for j in range(len(a_) - 1):
                    if sgn > 0:                         # facing up and in, towards the road
                        walls.quad(a_[j], b_[j], b_[j + 1], a_[j + 1])
                    else:
                        walls.quad(a_[j], a_[j + 1], b_[j + 1], b_[j])
        rows = []
        for k in range(r0, r0 + per + 1):
            pos, left, up, u = frames[k]
            v = road_rep * (k - r0) / float(per)
            rows.append((road.vert(G.add(pos, G.mul(left, ROAD_W)), up, (0, v), (255, 255, 255)),
                         road.vert(G.add(pos, G.mul(left, -ROAD_W)), up, (1, v), (255, 255, 255))))
        for (a_, b_), (c_, d_) in zip(rows, rows[1:]):
            road.quad(a_, b_, d_, c_)
        M.smooth_normals(walls)
        m_chunks.append(G.add_model(s, [(road, t_dirt, 0), (walls, t_rock, 0)], light=LIGHT))

    def ground(d, lat):
        """A point on the canyon floor lat metres to the left of the road's centre (- to the right)."""
        pos, fwd, up = G.track_at(tr, d)
        left = G.norm(G.cross(up, fwd))
        side, sgn = (0, 1.0) if lat >= 0 else (1, -1.0)
        prof = profile(side, (d % length) / length, 900)
        off = abs(lat) - ROAD_W
        h = 0.0
        for (o0, h0), (o1, h1) in zip(prof, prof[1:]):
            if o0 <= off <= o1:
                h = h0 + (h1 - h0) * (off - o0) / (o1 - o0)
                break
        return G.add(G.add(pos, G.mul(left, lat)), (0, h if off > 0 else 0, 0)), fwd

    # ---- Arches, boulders, cacti -----------------------------------------------------------------------
    arch = M.torus(ROAD_W + 11, 3.2, 20, 6, uvs=6).transform(rot=(0, 90, 0))
    arch.pos = [p for p in arch.pos]
    keep = [t for t in arch.tris if all(arch.pos[i][1] > -1.0 for i in t)]   # the upper half
    arch.tris = keep
    M.displace(arch, lambda p: 1.2 * G.noise3(G.mul(p, 0.25), 77))
    m_arch = G.add_model(s, [(arch, t_rock, 0)], light=LIGHT)
    m_boulders = [G.add_model(s, [(M.rock(1, 80 + k), t_boulder, 0)], light=LIGHT) for k in range(4)]
    m_cacti = [G.add_model(s, [(saguaro(rng), t_cactus, 0)], light=LIGHT) for _ in range(3)]
    I = s.add
    arches = [length * f for f in (0.12, 0.47, 0.81)]
    for d in arches:
        pos, fwd, up = G.track_at(tr, d)
        yaw = math.degrees(math.atan2(fwd[0], fwd[2]))
        I("instances", model=m_arch, p=pos + (yaw, 0, 0, 1))
    for k in range(70):
        d = rng.uniform(0, length)
        lat = rng.choice((-1, 1)) * rng.uniform(ROAD_W + 2.5, ROAD_W + 9)
        p, fwd = ground(d, lat)
        size = rng.uniform(0.8, 3.2)
        I("instances", model=rng.choice(m_boulders), p=G.add(p, (0, size * 0.4, 0)) + (rng.uniform(0, 360), 0, 0, size))
    for k in range(70):
        d = rng.uniform(0, length)
        lat = rng.choice((-1, 1)) * rng.uniform(ROAD_W + 3.2, ROAD_W + 7)
        p, fwd = ground(d, lat)
        I("instances", model=rng.choice(m_cacti), p=p + (rng.uniform(0, 360), 0, 0, rng.uniform(0.8, 1.3)))
    for c, m in enumerate(m_chunks):
        I("instances", model=m, p=(0, 0, 0, 0, 0, 0, 1))

    # ---- The buggies --------------------------------------------------------------------------------------
    paints = [(220, 60, 50), (40, 110, 210), (240, 200, 40), (60, 170, 80), (230, 120, 30), (200, 200, 210)]
    m_cars = [G.add_model(s, [(buggy(c), t_body, 0)], lit=True) for c in paints]
    wheel = M.cylinder(0.45, 0.34, 10, uvs=0).transform(rot=(0, 0, 90))
    wheel.map_uv(lambda p, uv: (0.5 + p[2] / 1.0, 0.5 + p[1] / 1.0) if abs(p[0]) > 0.16 else uv)
    m_wheel = G.add_model(s, [(wheel.colour((255, 255, 255)), t_tyre, 0)], lit=True)
    P = lambda **kw: I("parts", **kw)
    p_dust = P(tex=t_dust, life=1.2, life_jitter=0.3, size0=1.0, size1=4.5, speed=3.0, speed_jitter=0.5, spread=0.9,
               drag=0.9, rise=0.7, rgba0=0xC8A87890, rgba1=0xB4966C00, fade_in=0.1, spin=40)
    p_fire = P(tex=t_fire, flags=dbs.PF_ADD, life=0.25, size0=0.6, size1=1.6, speed=6, spread=0.5,
               rgba0=0xFFFFFFFF, rgba1=0x80200000)
    cars = []
    for k in range(6):
        speed = 34.0 + rng.uniform(-1.5, 4.0)
        lane = (-3.2, 0.0, 3.2)[k % 3]
        d0 = 120.0 - k * 14.0
        surge = rng.uniform(4, 12)
        i = I("instances", model=m_cars[k], kind=dbs.IK_TRACK, track=0,
              p=(d0, speed, lane, 0.45, 1, surge, rng.uniform(0.05, 0.12), rng.uniform(0, 6.3)))
        cars.append(i)
        rate = speed / 0.45 * 180 / math.pi
        for x, z in ((0.95, 1.35), (-0.95, 1.35), (0.95, -1.35), (-0.95, -1.35)):
            I("instances", model=m_wheel, kind=dbs.IK_SPIN, parent=i,
              p=(x, 0.45 - 0.45, z, 0, 0, 0, 1, 1, 0, 0, rate, rng.uniform(0, 360)))
        for x in (0.8, -0.8):
            I("emitters", part=p_dust, inst=i, pos=(x, 0.15, -1.6), dir=(0, 0.5, -1), rate=12, seed=k * 10 + int(x > 0))
        for _ in range(4):
            I("bursts", frame=rng.randint(20, FRAMES - 20), part=p_fire, inst=i, pos=(0.3, 0.6, -1.9), count=6,
              seed=rng.randint(0, 1 << 30))

    # ---- The sky, around the camera ---------------------------------------------------------------------------
    sun = M.quad(160, 160).transform(rot=(math.degrees(math.atan2(SUN[0], SUN[2])) + 180,
                                          -math.degrees(math.asin(SUN[1])), 0), move=G.mul(SUN, 560))
    m_sky = G.add_model(s, [(sky_dome(), dbs.NONE, dbs.BF_SKY | dbs.BF_TWOSIDED, None),
                            (sun, t_sun, dbs.BF_ADD | dbs.BF_TWOSIDED, None)])

    # ---- Shots ------------------------------------------------------------------------------------------------
    lead = cars[0]
    cam_tr = len(s.tracks)                          # the helicopter: above and behind the pack
    heli = [G.add(G.track_point(tr, 60 + 34.5 * f / RATE, 0, 0), (0, 32, 0)) for f in range(0, 160, 20)]
    s.tracks.append(G.track(heli, spacing=6.0))
    side_d = 120 + 34.5 * 330 / RATE
    side, _ = ground(side_d, -(ROAD_W + 6))
    arch_d = min(arches, key=lambda d: (d - (120 + 34.5 * 690 / RATE)) % length)
    under, _ = ground(arch_d - 6, 2.0)
    s.shots += [dict(f0=0, f1=140, kind=dbs.CK_PATH, track=cam_tr, target=lead, fov=55,
                     p=(0, s.tracks[cam_tr]["length"] / (140 / RATE), 0)),
                dict(f0=140, f1=280, kind=dbs.CK_CHASE, target=cars[1], p=(9.5, 3.2, 22, 0.8, 0)),
                dict(f0=280, f1=400, kind=dbs.CK_FIXED, target=lead, fov=48, p=G.add(side, (0, 1.6, 0)) + (0.8,)),
                dict(f0=400, f1=540, kind=dbs.CK_MOUNT, target=cars[2], p=(0, 0.55, 2.4, 0, -0.04, 1)),
                dict(f0=540, f1=680, kind=dbs.CK_ORBIT, target=lead, p=(0, 0, 0, 13, 3.5, 32, 30, 0.8)),
                dict(f0=680, f1=820, kind=dbs.CK_FIXED, target=lead, fov=62, p=G.add(under, (0, 1.2, 0)) + (0.8,)),
                dict(f0=820, f1=FRAMES, kind=dbs.CK_CHASE, target=cars[3], p=(8.0, 2.6, 25, 0.8, 1.5))]
    s.game = dict(frames=FRAMES, rate=RATE, clear_rgb=0, flags=dbs.GF_FOG, fovy=58.0, znear=0.5, zfar=700.0,
                  fog_rgb=(HORIZON[0] << 16) | (HORIZON[1] << 8) | HORIZON[2], fog_start=300.0, fog_end=650.0,
                  sun=SUN, sun_rgb=SUN_RGB, ambient=AMBIENT, seed=2402, sky=m_sky, capture=200)
    s.info = {"name": "CANYON"}
    G.budget(s, "CANYON")
    return s


if __name__ == "__main__":
    import sys
    build().write(sys.argv[1] if len(sys.argv) > 1 else "CANYON.DBS")
