"""G3SPAC Space battle (SPACE.DBS): procedural, 40 seconds at 25 frames/s.

A station with a spinning ring, a cruiser crossing at 12 m/s, an asteroid
field, and two teams of nine fighters on banked loops shooting it out:
laser bolts, five fighters lost (fireball, sparks, debris, smoke and a
shockwave), hits on the cruiser and a last explosion on its flank. The sky
(stars, nebulae, the sun, a banded planet) is drawn around the camera.
What it asks of a card: many objects each with its own transform and CPU
lighting (asteroids, fighters, the ring), many draw calls, additive
particles (docs/tests.md). Everything comes from PCG32 seeds: the same file
on every machine. Standard library only.
"""
import math

import dbs
import gsgen as G
import meshgen as M
import texgen as T

RATE = 25.0
FRAMES = 1000
SUN = G.norm((0.6, 0.45, 0.65))
SUN_RGB = (1.15, 1.1, 1.0)
AMBIENT = (0.12, 0.13, 0.18)


def light_for(fwd=(0, 0, 1), up=(0, 1, 0)):
    """Baked light for a model placed facing fwd with up: the sun in its own space."""
    left = G.norm(G.cross(up, fwd))
    return ((G.dot(SUN, left), G.dot(SUN, up), G.dot(SUN, fwd)), SUN_RGB, AMBIENT)


def noise3(p, seed):
    """Smooth 3D value noise in [-1, 1] (rock shapes)."""
    def h(x, y, z):
        return T.hash3(x * 73856093 + z * 19349663, y, seed) / 2147483647.5 - 1
    xi, yi, zi = (int(math.floor(c)) for c in p)
    f = [c - math.floor(c) for c in p]
    f = [t * t * (3 - 2 * t) for t in f]
    acc = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[0] if dx else 1 - f[0]) * (f[1] if dy else 1 - f[1]) * (f[2] if dz else 1 - f[2])
                acc += w * h(xi + dx, yi + dy, zi + dz)
    return acc


def rock_mesh(subdiv, seed):
    m = M.icosphere(1.0, subdiv)
    M.displace(m, lambda p: 0.28 * noise3(G.mul(p, 1.6), seed) + 0.12 * noise3(G.mul(p, 3.7), seed + 1))
    m.map_uv(lambda p, uv: (p[0] * 0.45 + p[2] * 0.3, p[1] * 0.5 + p[2] * 0.2))
    return m


def fighter_mesh(col):
    parts = [M.box(2.0, 1.6, 9.0, uvs=2.0), M.wedge(2.0, 1.6, 3.0).transform(move=(0, -0.1, 6.0)),
             M.box(1.2, 0.8, 2.2, uvs=1.0).transform(move=(0, 1.1, 1.5)),
             M.box(10.0, 0.3, 3.0, uvs=2.0).transform(move=(0, -0.2, -1.0)),
             M.box(0.3, 2.2, 2.0, uvs=1.0).transform(move=(0, 1.4, -3.5))]
    for x in (-5.0, 5.0):
        parts.append(M.cylinder(0.22, 3.2, 6, uvs=1.0).transform(rot=(0, 90, 0), move=(x, -0.2, 0.8)))
    for x in (-1.2, 1.2):
        parts.append(M.cylinder(0.6, 3.0, 8, uvs=1.0).transform(rot=(0, 90, 0), move=(x, 0, -4.5)))
    m = M.Mesh()
    for p in parts:
        m.merge(p)
    return m.colour(col)


def cruiser_mesh(rng):
    hull = [M.box(70, 45, 280, uvs=24), M.wedge(70, 45, 120).transform(move=(0, 0, 200)),
            M.box(40, 20, 200, uvs=24).transform(move=(0, 32, -20)),
            M.box(24, 36, 30, uvs=12).transform(move=(0, 58, -90)),
            M.box(40, 8, 16, uvs=12).transform(move=(0, 80, -90)),
            M.box(90, 55, 40, uvs=24).transform(move=(0, 0, -160))]
    for x in (-45, 45):
        hull.append(M.box(20, 16, 120, uvs=16).transform(move=(x, -5, 10)))
    for x in (-28, 0, 28):
        hull.append(M.cylinder(11, 16, 12, uvs=12).transform(rot=(0, 90, 0), move=(x, 0, -188)))
    for k in range(10):                                     # turrets along the deck
        z = -60 + k * 22
        x = -14 if k % 2 else 14
        hull.append(M.box(8, 5, 8, uvs=4).transform(move=(x, 44.5, z)))
        hull.append(M.cylinder(0.8, 10, 6, caps=False, uvs=4).transform(rot=(0, 90, 0), move=(x, 45, z + 8)))
    for _ in range(140):                                    # greebles on the hull's faces
        side = rng.randint(0, 3)
        w, h, l = rng.uniform(3, 10), rng.uniform(1.5, 5), rng.uniform(5, 24)
        z = rng.uniform(-130, 130)
        if side == 0:
            pos = (rng.uniform(-30, 30), 22.5 + h / 2, z)
        elif side == 1:
            pos = (rng.uniform(-30, 30), -22.5 - h / 2, z)
        else:
            pos = ((35 + w / 2) * (1 if side == 2 else -1), rng.uniform(-15, 15), z)
        hull.append(M.box(w, h, l, uvs=6).transform(move=pos))
    hull.append(M.box(1.2, 30, 1.2, uvs=4).transform(move=(6, 98, -90)))
    hull.append(M.box(1.2, 20, 1.2, uvs=4).transform(move=(-8, 93, -94)))
    m = M.Mesh()
    for p in hull:
        m.merge(p)
    return m.colour((200, 200, 205))


def cruiser_windows():
    """Additive window strips along both sides, just off the hull."""
    m = M.Mesh()
    for x, ry in ((35.3, 90), (-35.3, -90)):
        for z in (-90, 10, 110):
            m.merge(M.quad(90, 10, uv=((0, 0), (3, 0.6))).transform(rot=(ry, 0, 0), move=(x, 6, z)))
    return m


def station_hub():
    parts = [M.cylinder(30, 160, 16, uvs=20), M.cylinder(30, 50, 16, uvs=20, r2=8).transform(move=(0, 105, 0)),
             M.cylinder(8, 50, 16, uvs=20, r2=30).transform(move=(0, -105, 0)),
             M.box(4, 80, 4, uvs=8).transform(move=(0, 170, 0))]
    for a in range(4):
        parts.append(M.box(14, 14, 60, uvs=10).transform(rot=(45 + 90 * a, 0, 0), move=(
            42 * math.sin(math.radians(45 + 90 * a)), -40, 42 * math.cos(math.radians(45 + 90 * a)))))
    m = M.Mesh()
    for p in parts:
        m.merge(p)
    return m.colour((185, 185, 195))


def station_ring():
    m = M.torus(170, 12, 72, 10, uvs=24)
    for a in range(4):
        m.merge(M.box(6, 6, 140, uvs=10).transform(rot=(90 * a, 0, 0), move=(
            100 * math.sin(math.radians(90 * a)), 0, 100 * math.cos(math.radians(90 * a)))))
    return m.colour((200, 200, 210))


def sky_parts(t_stars, t_neb, t_neb2, t_sun, t_planet):
    """The camera-centred sky: stars, nebulae, the sun's glow, the planet (in that order: no depth test)."""
    stars = M.box(6000, 6000, 6000, uvs=0).map_uv(lambda p, uv: (uv[0] * 2 + (p[0] + p[2]) * 1e-4, uv[1] * 2))
    stars.colour((255, 255, 255))
    neb = M.quad(3200, 3200).transform(rot=(200, 10, 0), move=G.mul(G.norm((-0.3, 0.25, -1)), 2900))
    neb2 = M.quad(2600, 2000).transform(rot=(120, -15, 30), move=G.mul(G.norm((0.8, 0.1, -0.6)), 2900))
    sund = SUN
    yaw = math.degrees(math.atan2(sund[0], sund[2]))
    pitch = -math.degrees(math.asin(sund[1]))
    sun = M.quad(900, 900).transform(rot=(yaw + 180, -pitch, 0), move=G.mul(sund, 2900))
    pdir = G.norm((-0.45, -0.15, 0.88))
    planet = M.sphere(1100, 32, 16).transform(rot=(0, 0, 18), move=G.mul(pdir, 2600)).colour((255, 255, 255))
    return [(stars, t_stars, dbs.BF_SKY | dbs.BF_TWOSIDED, None), (neb, t_neb, dbs.BF_ADD | dbs.BF_TWOSIDED, None),
            (neb2, t_neb2, dbs.BF_ADD | dbs.BF_TWOSIDED, None), (sun, t_sun, dbs.BF_ADD | dbs.BF_TWOSIDED, None),
            (planet, t_planet, 0, light_for() + (0.15,))]


def loop_track(rng, centre, rx, rz, y, wobble, n=9, spacing=10.0, phase=0.0):
    """A closed, banked loop round centre: an ellipse with jittered radius and height."""
    pts = []
    for i in range(n):
        a = phase + 2 * math.pi * i / n
        r = 1 + rng.uniform(-0.18, 0.18)
        pts.append((centre[0] + rx * r * math.cos(a), y + rng.uniform(-wobble, wobble), centre[2] + rz * r * math.sin(a)))
    tr = G.track(pts, closed=True, spacing=spacing)
    # Bank into the turns: roll from the change of heading per metre.
    keys = tr["keys"]
    n = len(keys)
    out = []
    for i in range(n):
        a, b, c = keys[i - 1][:3], keys[i][:3], keys[(i + 1) % n][:3]
        h1, h2 = math.atan2(b[0] - a[0], b[2] - a[2]), math.atan2(c[0] - b[0], c[2] - b[2])
        dh = (h2 - h1 + math.pi) % (2 * math.pi) - math.pi
        roll = max(-55.0, min(55.0, math.degrees(dh) * 9.0))      # +x is left: turning to it leans left
        out.append(b + (roll,))
    tr["keys"] = out
    return tr


def build(args=None):
    rng = G.PCG32(31337)
    s = dbs.Scene()
    tex = s.add_texture
    t_stars = tex(256, 256, T.stars(256, 11, 0.005), 0)
    t_neb = tex(128, 128, T.nebula(128, 12, (110, 50, 150)), dbs.TF_CLAMP)
    t_neb2 = tex(128, 128, T.nebula(128, 13, (40, 90, 140)), dbs.TF_CLAMP)
    t_sun = tex(64, 64, T.glow(64, (255, 240, 200), 1.6), dbs.TF_CLAMP)
    t_planet = tex(256, 128, T.planet(256, 128, 14))
    t_hull = tex(256, 256, T.plating(256, 15, (150, 152, 160)))
    t_station = tex(128, 128, T.plating(128, 16, (170, 165, 150), panels=4))
    t_windows = tex(128, 128, T.windows(128, 17))
    t_rock = tex(128, 128, T.rock(128, 18))
    t_blue = tex(64, 64, T.plating(64, 19, (90, 120, 190), panels=2))
    t_red = tex(64, 64, T.plating(64, 20, (190, 90, 80), panels=2))
    t_bolt_r = tex(32, 128, T.bolt(32, 128, (255, 60, 40)), dbs.TF_CLAMP)
    t_bolt_g = tex(32, 128, T.bolt(32, 128, (60, 255, 90)), dbs.TF_CLAMP)
    t_glow = tex(32, 32, T.glow(32, (255, 255, 255)), dbs.TF_CLAMP)
    t_engine = tex(32, 32, T.glow(32, (120, 170, 255), 1.5), dbs.TF_CLAMP)
    t_beacon = tex(32, 32, T.glow(32, (255, 60, 40), 1.5), dbs.TF_CLAMP)
    t_fire = tex(64, 64, T.fire(64, 21), dbs.TF_CLAMP)
    t_smoke = tex(64, 64, T.smoke(64, 22, 150), dbs.TF_CLAMP)
    t_debris = tex(32, 32, T.debris(32, 23), dbs.TF_CLAMP)
    t_ring = tex(64, 64, T.ring(64), dbs.TF_CLAMP)

    # ---- Models ----------------------------------------------------------------------
    m_sky = G.add_model(s, sky_parts(t_stars, t_neb, t_neb2, t_sun, t_planet))
    m_hub = G.add_model(s, [(station_hub(), t_station, 0)], light=light_for())
    m_ring = G.add_model(s, [(station_ring(), t_station, 0),
                             (M.torus(170, 12.4, 72, 4, uvs=36), t_windows, dbs.BF_ADD)], lit=True)
    # The cruiser goes +x on its track: its +z is the world's +x.
    m_cruiser = G.add_model(s, [(cruiser_mesh(rng), t_hull, 0), (cruiser_windows(), t_windows,
                                                                  dbs.BF_ADD | dbs.BF_TWOSIDED)],
                            light=light_for(fwd=(1, 0, 0)))
    m_beacon = G.add_model(s, [(M.crossed(9, 9).merge(M.quad(9, 9)), t_beacon, dbs.BF_GLOW | dbs.BF_TWOSIDED)])
    m_fighter_b = G.add_model(s, [(fighter_mesh((210, 215, 230)), t_blue, 0)], lit=True)
    m_fighter_r = G.add_model(s, [(fighter_mesh((230, 210, 205)), t_red, 0)], lit=True)
    m_bolt_r = G.add_model(s, [(M.crossed(1.0, 14), t_bolt_r, dbs.BF_ADD | dbs.BF_TWOSIDED)])
    m_bolt_g = G.add_model(s, [(M.crossed(1.0, 14), t_bolt_g, dbs.BF_ADD | dbs.BF_TWOSIDED)])
    rocks = []                                              # (big, small) entry models per shape
    for shape in range(6):
        entry = []
        for near, far in ((650.0, 1500.0), (320.0, 850.0)):
            lo = G.add_model(s, [(rock_mesh(0, 40 + shape), t_rock, 0)], lit=True)
            mid = G.add_model(s, [(rock_mesh(1, 40 + shape), t_rock, 0)], lit=True, lod_next=lo, lod_dist=far)
            hi = G.add_model(s, [(rock_mesh(2, 40 + shape), t_rock, 0)], lit=True, lod_next=mid, lod_dist=near)
            entry.append(hi)
        rocks.append(entry)
    s.surfaces.append(dict(batch=s.models[m_beacon]["batch0"], kind=dbs.SK_PULSE, p=(0.15, 0.85, 0.8, 0)))

    # ---- Tracks ----------------------------------------------------------------------
    cruiser_track = len(s.tracks)
    s.tracks.append(G.track([(-1100, 60, 650), (-700, 60, 650), (-300, 60, 650), (100, 60, 650)], spacing=20))
    squad_tracks = []
    plans = [  # (centre, rx, rz, height, team)
        ((-300, 0, 250), 900, 650, 40, "b"), ((-150, 0, 100), 650, 900, 120, "b"), ((-500, 0, 350), 750, 500, -40, "b"),
        ((-700, 0, 650), 520, 380, 110, "r"), ((-650, 0, 600), 600, 300, 20, "r"), ((-400, 0, 500), 820, 520, 70, "r")]
    for k, (c, rx, rz, y, team) in enumerate(plans):
        squad_tracks.append(len(s.tracks))
        s.tracks.append(loop_track(rng, c, rx, rz, y, 60, phase=k * 0.9))
    cam1 = len(s.tracks)
    # On the sun's side of the station (+x, +z), the field and the cruiser beyond it.
    s.tracks.append(G.track([(720, 230, 260), (560, 150, 380), (400, 90, 400), (300, 50, 330)], spacing=10))
    cam6 = len(s.tracks)
    s.tracks.append(G.track([(-1300, 135, 790), (-900, 120, 780), (-500, 110, 770), (-100, 110, 760)], spacing=20))

    # ---- Instances -------------------------------------------------------------------------
    I = s.add
    i_hub = I("instances", model=m_hub, p=(0, 0, 0, 0, 0, 0, 1))
    I("instances", model=m_ring, kind=dbs.IK_SPIN, parent=i_hub, p=(0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 6, 0))
    for y in (130, -130):
        I("instances", model=m_beacon, parent=i_hub, p=(0, y, 0, 0, 0, 0, 1))
    i_cruiser = I("instances", model=m_cruiser, kind=dbs.IK_TRACK, track=cruiser_track, p=(0, 12, 0, 0, 1))
    for _ in range(200):                                    # the field, kept off the station and the cruiser's lane
        while True:
            a, r = rng.uniform(0, 2 * math.pi), math.sqrt(rng.random()) * 750
            p = (-380 + r * math.cos(a), rng.uniform(-110, 110), 280 + r * math.sin(a))
            if G.length((p[0], 0, p[2])) > 260 and not (abs(p[2] - 650) < 90 and abs(p[1] - 60) < 70):
                break
        size = rng.uniform(4, 34) if rng.random() < 0.7 else rng.uniform(4, 12)
        entry = rng.choice(rocks)[0 if size > 14 else 1]
        axis = rng.unit()
        I("instances", model=entry, kind=dbs.IK_SPIN,
          p=(p[0], p[1], p[2], rng.uniform(0, 360), rng.uniform(-60, 60), rng.uniform(0, 360), size,
             axis[0], axis[1], axis[2], rng.uniform(4, 22) * (1 if rng.random() < 0.5 else -1), rng.uniform(0, 360)))
    fighters = []                                           # (instance, team, track, d0, speed, lateral, height)
    for k, tr_i in enumerate(squad_tracks):
        team = plans[k][4]
        speed = rng.uniform(105, 135)
        d0 = rng.uniform(0, s.tracks[tr_i]["length"])
        for w, (back, lat, up) in enumerate(((0, 0, 0), (-28, 16, 3), (-28, -16, -3))):
            i = I("instances", model=m_fighter_b if team == "b" else m_fighter_r, kind=dbs.IK_TRACK, track=tr_i,
                  p=(d0 + back, speed, lat, up, 1))
            fighters.append(dict(inst=i, team=team, track=tr_i, d0=d0 + back, speed=speed, lat=lat, up=up))

    # ---- Effects ----------------------------------------------------------------------------
    P = lambda **kw: I("parts", **kw)
    p_engine = P(tex=t_engine, flags=dbs.PF_ADD, life=0.12, size0=5, size1=4, rgba0=0xFFFFFFFF)
    p_cglow = P(tex=t_engine, flags=dbs.PF_ADD, life=0.2, size0=46, size1=40, rgba0=0xFFFFFFFF)
    p_exhaust = P(tex=t_engine, flags=dbs.PF_ADD, life=1.4, life_jitter=0.2, size0=16, size1=3, speed=45,
                  speed_jitter=0.2, spread=0.08, rgba0=0xC0D0FFFF, rgba1=0x10204000)
    p_fire = P(tex=t_fire, flags=dbs.PF_ADD, life=0.9, life_jitter=0.3, size0=5, size1=26, speed=14, speed_jitter=0.6,
               spread=math.pi, drag=1.5, rgba0=0xFFFFFFFF, rgba1=0x60200000, spin=120)
    p_spark = P(tex=t_glow, flags=dbs.PF_ADD, life=0.7, life_jitter=0.4, size0=2.5, size1=0.8, speed=70,
                speed_jitter=0.5, spread=math.pi, drag=0.6, rgba0=0xFFE0A0FF, rgba1=0xFF602000)
    p_debris = P(tex=t_debris, flags=dbs.PF_ALPHATEST, life=2.8, life_jitter=0.3, size0=2.6, size1=2.6, speed=28,
                 speed_jitter=0.6, spread=math.pi, drag=0.3, rgba0=0xA0A0A0FF, spin=400)
    p_smoke = P(tex=t_smoke, life=2.6, life_jitter=0.3, size0=7, size1=34, speed=6, speed_jitter=0.5,
                spread=math.pi, drag=0.8, rgba0=0x605850B0, rgba1=0x30303000, fade_in=0.08, spin=40)
    p_ring = P(tex=t_ring, flags=dbs.PF_ADD | dbs.PF_FLAT, life=0.7, size0=6, size1=110, rgba0=0xFFFFFFFF,
               rgba1=0x40608000)
    p_hit = P(tex=t_glow, flags=dbs.PF_ADD, life=0.35, size0=7, size1=2, speed=30, speed_jitter=0.5,
              spread=1.2, rgba0=0xFFF0C0FF, rgba1=0xFF804000)
    for f in fighters:
        I("emitters", part=p_engine, inst=f["inst"], pos=(0, 0, -6.4), dir=(0, 0, -1), rate=25, flags=dbs.EF_MOVE,
          seed=f["inst"])
    for k, x in enumerate((-28, 0, 28)):
        I("emitters", part=p_cglow, inst=i_cruiser, pos=(x, 0, -198), dir=(0, 0, -1), rate=15, flags=dbs.EF_MOVE,
          seed=100 + k)
        I("emitters", part=p_exhaust, inst=i_cruiser, pos=(x, 0, -200), dir=(0, 0, -1), rate=14, seed=110 + k)

    def explode(frame, inst, pos=(0, 0, 0), scale=1.0, seed=0):
        for part, count in ((p_fire, 14), (p_spark, 36), (p_debris, 14), (p_smoke, 9), (p_ring, 1)):
            I("bursts", frame=frame, part=part, inst=inst, pos=pos, count=count, seed=seed * 31 + part, scale=scale)

    # Five fighters lost, at times the shots see them; each fighter fires bursts before that.
    losses = {fighters[10]["inst"]: 520, fighters[4]["inst"]: 330, fighters[13]["inst"]: 640,
              fighters[16]["inst"]: 700, fighters[7]["inst"]: 205}
    for f in fighters:
        end = losses.get(f["inst"], FRAMES)
        if end < FRAMES:
            s.instances[f["inst"]]["f1"] = end
            explode(end - 1, f["inst"], seed=f["inst"])
        model = m_bolt_g if f["team"] == "b" else m_bolt_r
        fire = rng.randint(10, 60)
        while fire + 12 < end:
            for burst in range(3):                          # three twin shots, 4 frames apart
                fr = fire + burst * 4
                if fr + 12 >= end:
                    break
                d = f["d0"] + f["speed"] * fr / RATE + 9
                for side in (-5.0, 5.0):
                    I("instances", model=model, kind=dbs.IK_TRACK, track=f["track"], f0=fr, f1=fr + 12,
                      p=(d, f["speed"] + 260, f["lat"] + side, f["up"] - 0.2, 1))
            fire += rng.randint(60, 140)
    for k in range(14):                                     # hits along the cruiser's hull
        fr = rng.randint(120, 880)
        z = rng.uniform(-150, 180)
        side = 1 if rng.random() < 0.5 else -1
        I("bursts", frame=fr, part=p_hit, inst=i_cruiser, pos=(36 * side, rng.uniform(-15, 25), z), count=14,
          seed=500 + k)
    # The finale, on the side the last shot sees (the model's right, -x, is the world's +z).
    for k, (fr, z, sc) in enumerate(((898, 40, 3.0), (906, 80, 2.0), (915, -10, 2.4), (932, 120, 1.6))):
        explode(fr, i_cruiser, pos=(-37, 5, z), scale=sc, seed=900 + k)

    # ---- Shots --------------------------------------------------------------------------------
    # The chase and the cockpit follow whichever blue fighter flies past the most rocks in that shot.
    rock_pos = [x["p"][:3] for x in s.instances if x.get("kind") == dbs.IK_SPIN and x["model"] not in (m_ring,)]

    def busy(f, f0, f1):
        n = 0
        for fr in range(f0, f1, 10):
            q = G.track_point(s.tracks[f["track"]], f["d0"] + f["speed"] * fr / RATE, f["lat"], f["up"])
            n += sum(1 for r in rock_pos if G.length(G.sub(r, q)) < 220)
        return n
    alive = [f for f in fighters if f["team"] == "b" and f["inst"] not in losses]
    blue_lead = max(alive, key=lambda f: busy(f, 150, 300))["inst"]
    blue_mount = max([f for f in alive if f["inst"] != blue_lead], key=lambda f: busy(f, 425, 575))["inst"]
    shots = [dict(f0=0, f1=150, kind=dbs.CK_PATH, track=cam1, target=i_hub, fov=55,
                  p=(0, s.tracks[cam1]["length"] / (150 / RATE), 0)),
             dict(f0=150, f1=300, kind=dbs.CK_CHASE, target=blue_lead, p=(30, 8, 90, 2, 0)),
             dict(f0=300, f1=425, kind=dbs.CK_FIXED, target=i_cruiser, fov=50, p=(-760, 150, 420, 20)),
             dict(f0=425, f1=575, kind=dbs.CK_MOUNT, target=blue_mount, p=(0, 1.8, 7.5, 0, -0.03, 1)),
             dict(f0=575, f1=725, kind=dbs.CK_ORBIT, target=i_cruiser, p=(0, 0, 0, 560, 170, 9, 200, 0)),
             dict(f0=725, f1=875, kind=dbs.CK_PATH, track=cam6, look_track=cruiser_track, fov=58,
                  p=(500, 26, 0, 725 / RATE * 12 + 140, 12, 15)),
             dict(f0=875, f1=1000, kind=dbs.CK_FIXED, target=i_cruiser, fov=48, p=(-300, 330, 1350, 10))]
    s.shots += shots
    s.game = dict(frames=FRAMES, rate=RATE, clear_rgb=0x000000, flags=0, fovy=60.0, znear=2.0, zfar=6000.0,
                  sun=SUN, sun_rgb=SUN_RGB, ambient=AMBIENT, seed=31337, sky=m_sky, capture=640)
    s.info = {"name": "SPACE"}
    G.budget(s, "SPACE")
    return s


if __name__ == "__main__":
    import sys
    build().write(sys.argv[1] if len(sys.argv) > 1 else "SPACE.DBS")
