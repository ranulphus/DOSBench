"""G4CITY City at dusk (CITY.DBS): procedural, 36 seconds at 25 frames/s.

A grid of 14 x 14 blocks, towers downtown, a river with bridges, at dusk:
the sky darkens and the stars come out while the windows light up one by
one (every facade has a second, additive window pass), the street lamps
come on, 300 cars with head and tail lights circulate, and a helicopter
sweeps a searchlight over the streets. What it asks of a card: fill and
overdraw (facades twice, lights, haze), many small draws, additive sprites
(docs/tests.md). PCG32 seeds: the same file everywhere. Standard library only.
"""
import math

import dbs
import gsgen as G
import meshgen as M
import texgen as T

RATE = 25.0
FRAMES = 900
BLOCKS = 14
BLOCK = 60.0                                    # a block's side
STREET = 16.0
PITCH = BLOCK + STREET
HALF = BLOCKS * PITCH / 2.0
SUN = G.norm((0.8, 0.12, -0.3))                 # low, nearly gone
LIGHT = (SUN, (0.55, 0.4, 0.35), (0.32, 0.32, 0.42))
HAZE = (54, 50, 82)
RIVER_ROW = 9                                   # the blocks of this row are the river


def facade(size, seed, base, glass=False):
    """A wall of dark windows: 4 across and 8 storeys per tile, the frame in the wall's colour."""
    def fn(u, v, x, y):
        cu, cv = (u * 4) % 1.0, (v * 8) % 1.0
        win = 0.18 < cu < 0.82 and 0.22 < cv < 0.85
        n = T.fbm(u, v, seed, base=4, octaves=3)
        if win:
            k = 0.35 + 0.25 * n
            tint = (70, 90, 120) if glass else (45, 50, 60)
            return (tint[0] * k * 2, tint[1] * k * 2, tint[2] * k * 2, 255)
        k = 0.75 + 0.3 * n
        return (base[0] * k, base[1] * k, base[2] * k, 255)
    return T.image(size, size * 2, fn)


def lit_windows(size, seed):
    """Additive: the same window grid, each window lit warm, cool or not at all."""
    def fn(u, v, x, y):
        cu, cv = (u * 4) % 1.0, (v * 8) % 1.0
        if not (0.18 < cu < 0.82 and 0.22 < cv < 0.85):
            return (0, 0, 0, 255)
        h = T.hash3(int(u * 4), int(v * 8), seed) / 4294967295.0
        if h < 0.3:
            return (0, 0, 0, 255)
        c = (255, 215, 150) if h < 0.75 else (190, 215, 255)
        k = 0.55 + 0.45 * (T.hash3(int(u * 4), int(v * 8), seed + 1) / 4294967295.0)
        return (c[0] * k, c[1] * k, c[2] * k, 255)
    return T.image(size, size * 2, fn)


def asphalt(size, seed):
    """One block's ground: the pavement in the middle, half of each street round it, the
    dashed centre lines on the tile's edges (a street is STREET of PITCH metres)."""
    edge = STREET / 2.0 / PITCH
    def fn(u, v, x, y):
        n = T.fbm(u, v, seed, base=16, octaves=3)
        k = 0.55 + 0.25 * n
        c = (70 * k, 70 * k, 76 * k)
        e = min(u, 1 - u, v, 1 - v)
        if e > edge:
            c = (130 * k, 128 * k, 120 * k)
        elif (min(u, 1 - u) < 0.01 and (v * 16) % 1.0 < 0.5) or (min(v, 1 - v) < 0.01 and (u * 16) % 1.0 < 0.5):
            c = (200, 190, 120)
        return c + (255,)
    return T.image(size, size, fn)


def water(size, seed):
    def fn(u, v, x, y):
        n = T.fbm(u, v, seed, base=8, octaves=3)
        return (30 + 30 * n, 40 + 40 * n, 70 + 50 * n, 200)
    return T.image(size, size, fn)


def tower_box(w, h, d, x, z, uv_w=16.0, uv_h=24.0):
    """Walls only (no top, no bottom), texture by the metre: a whole number of tiles each way."""
    m = M.Mesh()
    hw, hd = w / 2.0, d / 2.0
    corners = [(x - hw, z - hd), (x + hw, z - hd), (x + hw, z + hd), (x - hw, z + hd)]
    for k in range(4):
        (x0, z0), (x1, z1) = corners[k], corners[(k + 1) % 4]
        L = math.hypot(x1 - x0, z1 - z0)
        n = G.norm((z1 - z0, 0, -(x1 - x0)))      # outwards: the corners go round counter-clockwise
        ru, rv = max(1, round(L / uv_w)), max(1, round(h / uv_h))
        a = m.vert((x0, 0, z0), n, (0, 0))
        b = m.vert((x1, 0, z1), n, (ru, 0))
        c = m.vert((x1, h, z1), n, (ru, rv))
        d_ = m.vert((x0, h, z0), n, (0, rv))
        m.quad(a, d_, c, b)
    return m


def build(args=None):
    rng = G.PCG32(1999)
    s = dbs.Scene()
    tex = s.add_texture
    t_conc = tex(128, 256, facade(128, 81, (150, 145, 135)))
    t_glass = tex(128, 256, facade(128, 82, (90, 110, 130), glass=True))
    t_brick = tex(128, 256, facade(128, 83, (140, 85, 65)))
    t_lit = tex(128, 256, lit_windows(128, 84))
    t_street = tex(256, 256, asphalt(256, 85))
    t_roof = tex(64, 64, T.plating(64, 86, (95, 95, 100), panels=2))
    t_water = tex(128, 128, water(128, 87))
    t_car = tex(64, 64, T.plating(64, 88, (230, 230, 230), panels=2))
    t_glow = tex(32, 32, T.glow(32, (255, 255, 255), 1.6), dbs.TF_CLAMP)
    t_beam = tex(32, 128, T.bolt(32, 128, (120, 120, 90)), dbs.TF_CLAMP)
    t_stars = tex(256, 256, T.stars(256, 89, 0.004), 0)

    def cell(i, j):
        return (-HALF + PITCH * i + STREET / 2 + BLOCK / 2, -HALF + PITCH * j + STREET / 2 + BLOCK / 2)

    # ---- Blocks, four to a model (walls, roofs, the window pass) ------------------------------------------
    I = s.add
    tallest = [0.0, None]

    def buildings(i, j, walls, lit, roofs):
        cx, cz = cell(i, j)
        dist = math.hypot(cx, cz) / HALF
        n = rng.choice((1, 2, 2, 3, 4))
        spots = [(0, 0)] if n == 1 else [(-14, -14), (14, 14), (14, -14), (-14, 14)][:n]
        for sx, sz in spots:
            fw = BLOCK - 6 if n == 1 else rng.uniform(18, 26)
            fd = BLOCK - 6 if n == 1 else rng.uniform(18, 26)
            h = max(12.0, rng.uniform(0.6, 1.4) * (190 * max(0.0, 1 - dist) ** 1.6 + 14))
            kind = rng.choice(("conc", "glass", "brick")) if h > 60 else rng.choice(("conc", "brick"))
            x, z = cx + sx, cz + sz
            walls.setdefault(kind, M.Mesh()).merge(tower_box(fw, h, fd, x, z))
            over = tower_box(fw + 0.3, h, fd + 0.3, x, z)       # just off the wall: no depth fight
            on = rng.uniform(1.0, 30.0)                         # when this building's lights come on
            over.uv2 = [(on + rng.uniform(0, 4.0), 0.0) for _ in over.pos]
            lit.merge(over)
            roofs.merge(M.box(fw, 0.6, fd, uvs=8).transform(move=(x, h + 0.3, z)))
            if h > 50:
                roofs.merge(M.box(fw * 0.4, 5, fd * 0.4, uvs=8).transform(move=(x, h + 3, z)))
            if h > tallest[0]:
                tallest[:] = [h, (x, h, z)]
    for bi in range(0, BLOCKS, 2):
        for bj in range(0, BLOCKS, 2):
            walls, lit, roofs = {}, M.Mesh(), M.Mesh()
            for i, j in ((bi, bj), (bi + 1, bj), (bi, bj + 1), (bi + 1, bj + 1)):
                if j != RIVER_ROW:
                    buildings(i, j, walls, lit, roofs)
            if not roofs.pos:
                continue
            parts = [(m, {"conc": t_conc, "glass": t_glass, "brick": t_brick}[k], 0) for k, m in sorted(walls.items())]
            parts.append((roofs.colour((255, 255, 255)), t_roof, 0))
            parts.append((lit, t_lit, dbs.BF_ADD, None))
            m_block = G.add_model(s, parts, light=LIGHT)
            last = s.models[m_block]["batch0"] + s.models[m_block]["nbatch"] - 1
            s.surfaces.append(dict(batch=last, kind=dbs.SK_RAMP, p=(1.5,)))
            I("instances", model=m_block, p=(0, 0, 0, 0, 0, 0, 1))

    # ---- Streets, river, bridges ------------------------------------------------------------------------------
    ground = M.Mesh()
    for i in range(BLOCKS):
        for j in range(BLOCKS):
            if j == RIVER_ROW:
                continue
            cx, cz = cell(i, j)
            ground.merge(M.quad(PITCH, PITCH).transform(rot=(0, -90, 0), move=(cx, 0, cz)))
    chunks = []
    for q in range(4):                              # four quadrants: culling and the haze keep fewer
        part = M.Mesh()
        for k in range(0, len(ground.tris), 2):
            a = ground.tris[k][0]
            x, z = ground.pos[a][0], ground.pos[a][2]
            if (x >= 0) == (q & 1 == 1) and (z >= 0) == (q & 2 == 2):
                base = len(part.pos)
                vs = sorted(set(ground.tris[k] + ground.tris[k + 1]))
                remap = {v: base + n for n, v in enumerate(vs)}
                for v in vs:
                    part.vert(ground.pos[v], ground.nrm[v], ground.uv[v], (255, 255, 255))
                part.tris += [tuple(remap[v] for v in ground.tris[k]), tuple(remap[v] for v in ground.tris[k + 1])]
        if part.pos:
            chunks.append(G.add_model(s, [(part, t_street, 0)], light=LIGHT))
    for m in chunks:
        I("instances", model=m, p=(0, 0, 0, 0, 0, 0, 1))
    rz = -HALF + PITCH * RIVER_ROW + PITCH / 2
    river = M.quad(BLOCKS * PITCH + 400, PITCH - 6, uv=((0, 0), (30, 2))).transform(rot=(0, -90, 0), move=(0, -2.5, rz))
    banks = M.box(BLOCKS * PITCH + 400, 6, PITCH - 6, uvs=64).transform(move=(0, -6, rz))
    m_river = G.add_model(s, [(river, t_water, dbs.BF_TRANS)], light=LIGHT)
    m_bed = G.add_model(s, [(banks.colour((40, 45, 60)), t_roof, 0)], light=LIGHT)
    I("instances", model=m_bed, p=(0, 0, 0, 0, 0, 0, 1))
    I("instances", model=m_river, p=(0, 0, 0, 0, 0, 0, 1))
    s.surfaces.append(dict(batch=s.models[m_river]["batch0"], kind=dbs.SK_WARP, p=(0.04, 2.5, 0.9)))
    bridge = M.box(STREET - 2, 1.5, PITCH + 4, uvs=8).merge(M.box(0.6, 1.2, PITCH + 4, uvs=4).transform(move=(STREET / 2 - 1.3, 1.3, 0))) \
        .merge(M.box(0.6, 1.2, PITCH + 4, uvs=4).transform(move=(-STREET / 2 + 1.3, 1.3, 0)))
    m_bridge = G.add_model(s, [(bridge.colour((170, 165, 155)), t_roof, 0)], light=LIGHT)
    for i in range(0, BLOCKS + 1, 2):
        I("instances", model=m_bridge, p=(-HALF + PITCH * i, -0.8, rz, 0, 0, 0, 1))

    # ---- Street lamps ----------------------------------------------------------------------------------------------
    lamp = M.box(0.25, 7, 0.25, uvs=4).transform(move=(0, 3.5, 0)).colour((90, 90, 95))
    m_none = s.add_model(len(s.batches), 0)         # far off, a pole is not worth drawing
    m_pole = G.add_model(s, [(lamp, t_roof, 0)], light=LIGHT, lod_next=m_none, lod_dist=200.0)
    glow = M.crossed(2.4, 2.4).merge(M.quad(2.4, 2.4)).transform(move=(0, 7.1, 0))
    glow.uv2 = None
    m_lampglow = G.add_model(s, [(glow.colour((255, 210, 140)), t_glow, dbs.BF_GLOW | dbs.BF_TWOSIDED, None)])
    s.surfaces.append(dict(batch=s.models[m_lampglow]["batch0"], kind=dbs.SK_PULSE, p=(1.0, 0.06, 0.7, 1)))
    for i in range(BLOCKS):
        for j in range(BLOCKS):
            if j == RIVER_ROW or (i + j) % 2:
                continue
            cx, cz = cell(i, j)
            for dx, dz in ((-BLOCK / 2 - 1, -BLOCK / 2 - 1), (BLOCK / 2 + 1, BLOCK / 2 + 1)):
                p = (cx + dx, 0, cz + dz)
                I("instances", model=m_pole, p=p + (0, 0, 0, 1))
                I("instances", model=m_lampglow, p=p + (0, 0, 0, 1), f0=int(RATE * rng.uniform(6, 12)))

    # ---- Traffic: rectangular loops along the streets --------------------------------------------------------------
    lights_only = None
    car = M.box(1.8, 0.7, 4.2, uvs=1.0).transform(move=(0, 0.55, 0)).merge(
        M.box(1.6, 0.55, 2.2, uvs=1.0).transform(move=(0, 1.15, -0.2)))
    front = M.quad(1.6, 0.35, (255, 240, 200, 255)).transform(move=(0, 0.7, 2.12))
    rear = M.quad(1.6, 0.3, (255, 40, 30, 255)).transform(rot=(180, 0, 0), move=(0, 0.7, -2.12))
    paints = [(150, 30, 30), (30, 60, 140), (200, 200, 205), (30, 30, 35), (180, 150, 40), (60, 110, 70)]
    m_far = G.add_model(s, [(front.copy().merge(rear.copy()), t_glow, dbs.BF_GLOW, None)])   # far off: only the lights
    m_cars = [G.add_model(s, [(car.copy().colour(c), t_car, 0), (front.copy().merge(rear.copy()), t_glow,
                                                                  dbs.BF_GLOW, None)], light=LIGHT,
                          lod_next=m_far, lod_dist=220.0) for c in paints]
    loops = []
    for k in range(22):
        i0, j0 = rng.randint(0, BLOCKS - 4), rng.randint(0, BLOCKS - 4)
        i1, j1 = i0 + rng.randint(2, 6), j0 + rng.randint(2, 6)
        i1, j1 = min(i1, BLOCKS), min(j1, BLOCKS)
        if j0 <= RIVER_ROW < j1 and (i0 % 2 or i1 % 2):
            continue                                # crossing the river only where the bridges are
        x0, x1 = -HALF + PITCH * i0, -HALF + PITCH * i1
        z0, z1 = -HALF + PITCH * j0, -HALF + PITCH * j1
        pts = []
        for (a, b), (c, d) in (((x0, z0), (x1, z0)), ((x1, z0), (x1, z1)), ((x1, z1), (x0, z1)), ((x0, z1), (x0, z0))):
            for t in (0.0, 0.25, 0.5, 0.75):        # straight sides: Catmull-Rom only rounds the corners
                pts.append((a + (c - a) * t, 0.0, b + (d - b) * t))
        loops.append(len(s.tracks))
        s.tracks.append(G.track(pts, closed=True, spacing=6.0))
    cars = []
    per = 300 // len(loops) + 1
    for li, tr in enumerate(loops):
        L = s.tracks[tr]["length"]
        for k in range(per):
            if len(cars) >= 300:
                break
            lane = 3.0 if k % 2 else -3.0                # the two directions share a street
            speed = rng.uniform(9, 16) * (1 if lane < 0 else -1)
            cars.append(I("instances", model=rng.choice(m_cars), kind=dbs.IK_TRACK, track=tr,
                          p=(rng.uniform(0, L), speed, lane, 0.0, 1, 0, 0, 0, 0 if speed > 0 else 180)))

    # ---- The helicopter ------------------------------------------------------------------------------------------------
    heli = M.Mesh().merge(M.box(2.2, 2.0, 6.0, uvs=1.0).transform(move=(0, 0, 0.5))).merge(
        M.box(0.6, 0.6, 6.0, uvs=1.0).transform(move=(0, 0.3, -5.5))).merge(
        M.box(0.2, 1.8, 1.2, uvs=1.0).transform(move=(0, 1.0, -8.2))).merge(
        M.box(0.15, 0.3, 3.5, uvs=1.0).transform(move=(1.0, -1.3, 0.5))).merge(
        M.box(0.15, 0.3, 3.5, uvs=1.0).transform(move=(-1.0, -1.3, 0.5)))
    m_heli = G.add_model(s, [(heli.colour((60, 65, 70)), t_car, 0)], lit=True)
    m_rotor = G.add_model(s, [(M.box(11, 0.08, 0.35, uvs=1.0).merge(M.box(0.35, 0.08, 11, uvs=1.0)).colour((40, 40, 40)),
                               t_car, 0)], lit=True)
    beam = M.cylinder(9.0, 60, 10, caps=False, uvs=0, r2=0.4).transform(rot=(0, 0, 0), move=(0, -30, 0))
    m_beam = G.add_model(s, [(beam.colour((150, 150, 120)), t_beam, dbs.BF_GLOW | dbs.BF_TWOSIDED, None)])
    spot = M.quad(22, 22).transform(rot=(0, -90, 0))
    m_spot = G.add_model(s, [(spot.colour((255, 250, 210)), t_glow, dbs.BF_GLOW | dbs.BF_TWOSIDED, None)])
    hpts = [(-500, 100, -420), (-150, 100, -300), (120, 100, -60), (260, 100, 260), (40, 100, 480), (-320, 100, 380),
            (-520, 100, 60)]                        # level: the searchlight's spot stays on the street
    heli_tr = len(s.tracks)
    s.tracks.append(G.track(hpts, closed=True, spacing=8.0))
    i_heli = I("instances", model=m_heli, kind=dbs.IK_TRACK, track=heli_tr, p=(0, 26, 0, 0, 1))
    I("instances", model=m_rotor, kind=dbs.IK_SPIN, parent=i_heli, p=(0, 1.25, 0.5, 0, 0, 0, 1, 0, 1, 0, 900, 0))
    # The searchlight points ahead and down; its spot is where it meets the street (the flight is level).
    I("instances", model=m_beam, parent=i_heli, flags=dbs.IF_NOCULL, p=(0, -1.6, 2.5, 0, -28, 0, 1))
    I("instances", model=m_spot, parent=i_heli, flags=dbs.IF_NOCULL, p=(0, -98, 52, 0, 0, 0, 1))

    # ---- The sky: dusk, the stars coming out ------------------------------------------------------------------------
    dome = M.sphere(1400, 24, 12)
    for k, p in enumerate(dome.pos):
        h = max(0.0, p[1] / 1400.0)
        warm = max(0.0, G.dot(G.norm((p[0], 0, p[2])), G.norm((SUN[0], 0, SUN[2])))) * (1 - h) ** 3
        c = T.mix(T.mix((60, 55, 95), (20, 24, 60), min(1.0, h * 2)), (230, 120, 70), warm * 0.85)
        dome.col[k] = (c[0], c[1], c[2], 255)
    dome.tris = [(a, c, b) for a, b, c in dome.tris]
    stars = M.box(1600, 1600, 1600, uvs=0).map_uv(lambda p, uv: (uv[0] * 3, uv[1] * 3))
    stars.tris = [t for t in stars.tris if all(stars.pos[v][1] > -10 for v in t)]
    stars.uv2 = [(rng.uniform(8, 30), 0.0) for _ in stars.pos]
    m_sky = G.add_model(s, [(dome, dbs.NONE, dbs.BF_SKY | dbs.BF_TWOSIDED, None),
                            (stars.colour((255, 255, 255)), t_stars, dbs.BF_ADD | dbs.BF_TWOSIDED, None)])
    s.surfaces.append(dict(batch=s.models[m_sky]["batch0"] + 1, kind=dbs.SK_RAMP, p=(6.0,)))

    # ---- Shots ------------------------------------------------------------------------------------------------------------
    tx, ty, tz = tallest[1]
    river_tr = len(s.tracks)
    s.tracks.append(G.track([(-560, 6, rz), (-300, 14, rz), (-40, 30, rz), (200, 60, rz)], spacing=8))
    cross = cell(6, 6)
    corner = (cross[0] - BLOCK / 2 - 4, 3.0, cross[1] - BLOCK / 2 - 4)
    near_car = min(cars, key=lambda c: G.length(G.sub(G.track_point(s.tracks[s.instances[c]["track"]],
                                                                     s.instances[c]["p"][0] + s.instances[c]["p"][1] * 16.2,
                                                                     s.instances[c]["p"][2]), corner)))
    mount_car = max(cars[:60], key=lambda c: abs(s.instances[c]["p"][1]))
    s.shots += [dict(f0=0, f1=180, kind=dbs.CK_ORBIT, fov=55, p=(0, 0, 0, 640, 300, 6, 225, 40)),
                dict(f0=180, f1=330, kind=dbs.CK_CHASE, target=i_heli, p=(30, 9, 60, -12, 0)),
                dict(f0=330, f1=480, kind=dbs.CK_FIXED, target=near_car, fov=60, p=corner + (0.8,)),
                dict(f0=480, f1=630, kind=dbs.CK_MOUNT, target=mount_car, p=(0, 1.4, 0.3, 0, -0.02, 1)),
                dict(f0=630, f1=780, kind=dbs.CK_ORBIT, p=(tx, ty * 0.55, tz, 180, ty * 0.25, 14, 30, 0)),
                dict(f0=780, f1=FRAMES, kind=dbs.CK_PATH, track=river_tr, fov=60,
                     p=(0, s.tracks[river_tr]["length"] / ((FRAMES - 780) / RATE), 0, 60))]
    hz = (HAZE[0] << 16) | (HAZE[1] << 8) | HAZE[2]
    s.game = dict(frames=FRAMES, rate=RATE, clear_rgb=hz, flags=dbs.GF_FOG, fovy=60.0, znear=1.0, zfar=1500.0,
                  fog_rgb=hz, fog_start=450.0, fog_end=1350.0, sun=SUN, sun_rgb=LIGHT[1], ambient=LIGHT[2], seed=1999,
                  sky=m_sky, capture=700)
    s.info = {"name": "CITY"}
    G.budget(s, "CITY")
    return s


if __name__ == "__main__":
    import sys
    build().write(sys.argv[1] if len(sys.argv) > 1 else "CITY.DBS")
