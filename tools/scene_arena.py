"""G1ARNA Arena (ARENA.DBS): LibreQuake's e0m1 as a game, 40 seconds at 25 frames/s.

The world is the level fly-through's (tools/bsp.py: two-pass lightmaps,
the map's own visibility), with Quake's two-layer scrolling sky and warped
water and lava. The player walks the map's tour with a rocket launcher in
hand: every soldier and dog that comes into range and in clear sight
(traced through the BSP) gets a rocket, with a smoke trail and an
explosion, and plays its death frames; soldiers shoot back first, dogs run
at the player. Exploding boxes go up when hit. Embers rise from the
torches. Two cut-aways watch fights from the side. The soldier, dog,
launcher and rocket are LibreQuake's MDLs (tools/quakemdl.py); their
lighting is done on the CPU. Content: LibreQuake, BSD-3-Clause.
Standard library only.
"""
import math

import bsp
import dbs
import gsgen as G
import meshgen as M
import quakemdl
import texgen as T

RATE = 25.0
FRAMES = 1000
ROCKET = 1000.0                                 # units per second, as Quake's
RANGE = 420.0
REFIRE = 0.6                                    # seconds between rockets
SUN = G.norm((0.3, 0.9, 0.25))

# Frame layouts (LibreQuake keeps id's numbering for its QuakeC): (first, count).
SOLDIER = dict(stand=(0, 8), death=(8, 10), run=(73, 8), shoot=(81, 9))
DOG = dict(death=(8, 9), run=(48, 12), stand=(69, 9))


def q2y(v):
    return (v[0], v[2], -v[1])


def y2q(v):
    return (v[0], -v[2], v[1])


def build(args=None):
    rng = G.PCG32(4242)
    s, world, info = bsp.world({"map": "maps/lq_e0m1.bsp", "sky_layers": True})
    names = info["tex_names"]

    def visible(a, b):
        return world.clear_line(y2q(a), y2q(b), step=12.0)

    # ---- Models -----------------------------------------------------------------------
    keep_s = sorted({f for a, n in SOLDIER.values() for f in range(a, a + n)})
    keep_d = sorted({f for a, n in DOG.values() for f in range(a, a + n)})
    m_soldier, _, fmap_s = quakemdl.add_mdl(s, quakemdl.load("progs/soldier.mdl")[0], 256, keep_s)
    m_dog, _, fmap_d = quakemdl.add_mdl(s, quakemdl.load("progs/dog.mdl")[0], 128, keep_d)
    launcher = quakemdl.load("progs/v_rock.mdl")[0]
    m_launcher, _, _ = quakemdl.add_mdl(s, launcher, 128)
    m_rocket, _, _ = quakemdl.add_mdl(s, quakemdl.load("progs/missile.mdl")[0], 64)
    t_crate = s.add_texture(64, 64, T.plating(64, 61, (150, 110, 60), panels=2))
    crate = M.box(32, 64, 32, uvs=0).transform(move=(16, 32, -16)).colour((255, 255, 255))
    m_crate = G.add_model(s, [(crate, t_crate, 0)], lit=True)
    t_fire = s.add_texture(64, 64, T.fire(64, 62), dbs.TF_CLAMP)
    t_smoke = s.add_texture(64, 64, T.smoke(64, 63, 170), dbs.TF_CLAMP)
    t_glow = s.add_texture(32, 32, T.glow(32, (255, 200, 120)), dbs.TF_CLAMP)

    def anim(fmap, seq, layout, fps=10.0, phase=0.0):
        a, n = layout[seq]
        return (fmap[a], n, fps, phase)

    # ---- The player's walk: the map's tour, the whole 40 seconds --------------------------------
    keys, _, _, length = bsp.camera_path(world)
    path = len(s.tracks)
    s.tracks.append(G.track([p for p, _ in keys], spacing=24.0))
    speed = s.tracks[path]["length"] / (FRAMES / RATE)

    def player(t):
        pos, fwd, up = G.track_at(s.tracks[path], speed * t)
        return pos, fwd

    def muzzle(t):
        pos, fwd = player(t)
        return G.add(G.add(pos, G.mul(fwd, 18)), (0, -10, 0))

    # ---- Targets: monsters and boxes, each shot at the first chance -----------------------------
    targets = []                                    # (kind, position on the floor, yaw, centre)
    for e in world.entities:
        cn = e.get("classname", "")
        if "origin" not in e or int(e.get("spawnflags", "0")) & 512:
            continue
        o = [float(v) for v in e["origin"].split()]
        yaw = 90 + float(e.get("angle", "0"))
        if cn in ("monster_army", "monster_dog"):
            floor = q2y((o[0], o[1], o[2] - 24))
            targets.append(("soldier" if cn == "monster_army" else "dog", floor, yaw, G.add(floor, (0, 26, 0))))
        elif cn == "misc_explobox":
            base = q2y((o[0], o[1], o[2]))
            targets.append(("box", base, 0.0, G.add(base, (16, 32, -16))))
    # Encounters: every ENCOUNTER units of the walk, two or three more ahead of the player, on the
    # floor (traced down through the BSP), in the open and in clear sight.
    def floor_below(p):
        q = y2q(p)
        if world.contents(q) != bsp.CONTENTS_EMPTY:
            return None
        for k in range(1, 64):
            if world.contents((q[0], q[1], q[2] - 4 * k)) != bsp.CONTENTS_EMPTY:
                return q2y((q[0], q[1], q[2] - 4 * k + 4))
        return None

    def roomy(f):
        q = y2q(G.add(f, (0, 30, 0)))
        return all(world.contents((q[0] + dx, q[1] + dy, q[2])) == bsp.CONTENTS_EMPTY
                   for dx, dy in ((20, 0), (-20, 0), (0, 20), (0, -20)))
    d = 300.0
    while d < s.tracks[path]["length"] - 500:
        pos, fwd, _ = G.track_at(s.tracks[path], d)
        placed = 0
        for tries in range(40):
            if placed >= 2 + rng.randint(0, 1):
                break
            a = math.radians(rng.uniform(-35, 35))
            r = rng.uniform(200, 360)
            h = G.norm((fwd[0], 0, fwd[2]))
            dirn = (h[0] * math.cos(a) + h[2] * math.sin(a), 0, -h[0] * math.sin(a) + h[2] * math.cos(a))
            f = floor_below(G.add(pos, G.mul(dirn, r)))
            if not f or abs(f[1] - (pos[1] - 40)) > 120 or not roomy(f):
                continue
            c = G.add(f, (0, 26, 0))
            if not visible(pos, c) or any(G.length(G.sub(c, x[3])) < 70 for x in targets):
                continue
            kind = "dog" if rng.random() < 0.35 else "soldier"
            face = math.degrees(math.atan2(pos[0] - f[0], pos[2] - f[2]))
            targets.append((kind, f, face, c))
            placed += 1
        d += 420.0
    def plan(targets):
        """Rockets: every 0.1 s, the nearest target in range and in clear sight, if the launcher is ready."""
        shots, busy_until = [], 0.0
        for t10 in range(0, int(FRAMES / RATE * 10)):
            t = t10 / 10.0
            if t < busy_until or t > FRAMES / RATE - 1.5:
                continue
            eye, _ = player(t)
            best = None
            for k, (kind, floor, yaw, c) in enumerate(targets):
                if any(k == x for _, x in shots):
                    continue
                d = G.length(G.sub(c, eye))
                if d < RANGE and visible(eye, c) and (best is None or d < best[0]):
                    best = (d, k)
            if best:
                shots.append((t + 0.4, best[1]))    # a moment to aim
                busy_until = t + REFIRE
        return shots

    def too_close(targets, shots):
        """Monsters the player would walk into before their rocket lands."""
        out = set()
        for k, (kind, floor, yaw, c) in enumerate(targets):
            if kind == "box":
                continue
            dead = next((tf + 0.5 for tf, x in shots if x == k), FRAMES / RATE)
            for t10 in range(0, int(dead * 10)):
                if G.length(G.sub(player(t10 / 10.0)[0], c)) < 140:
                    out.add(k)
                    break
        return out
    for _ in range(4):
        shots = plan(targets)
        drop = too_close(targets, shots)
        if not drop:
            break
        targets = [x for k, x in enumerate(targets) if k not in drop]
    I = s.add
    P = lambda **kw: I("parts", **kw)
    p_trail = P(tex=t_smoke, life=0.8, life_jitter=0.3, size0=5, size1=16, speed=6, spread=math.pi, rise=18,
                rgba0=0x908880A0, rgba1=0x50505000, fade_in=0.1)
    p_tail = P(tex=t_glow, flags=dbs.PF_ADD, life=0.08, size0=14, size1=10, rgba0=0xFFFFFFFF)
    p_fire = P(tex=t_fire, flags=dbs.PF_ADD, life=0.55, life_jitter=0.3, size0=24, size1=70, speed=70,
               speed_jitter=0.5, spread=math.pi, drag=3, rgba0=0xFFFFFFFF, rgba1=0x60200000, spin=90)
    p_spark = P(tex=t_glow, flags=dbs.PF_ADD, life=0.9, life_jitter=0.4, size0=4, size1=2, speed=260,
                speed_jitter=0.5, spread=math.pi, gravity=420, drag=0.8, rgba0=0xFFD080FF, rgba1=0xA0300000)
    p_smoke = P(tex=t_smoke, life=1.8, life_jitter=0.3, size0=20, size1=70, speed=25, speed_jitter=0.5,
                spread=math.pi, rise=30, drag=1.0, rgba0=0x403830B0, rgba1=0x20202000, fade_in=0.1, spin=30)
    p_ember = P(tex=t_glow, flags=dbs.PF_ADD, life=1.4, life_jitter=0.4, size0=3, size1=1.5, speed=12,
                spread=0.6, rise=40, rgba0=0xFFB050FF, rgba1=0xFF300000)

    def explode(frame, pos, scale=1.0, seed=0):
        for part, count in ((p_fire, 8), (p_spark, 50), (p_smoke, 4)):
            I("bursts", frame=frame, part=part, pos=pos, count=count, seed=seed * 7 + part, scale=scale)

    hit_at = {}
    for n, (tf, k) in enumerate(shots):
        kind, floor, yaw, c = targets[k]
        a = muzzle(tf)                              # seen first a frame's flight out, as in the game
        a = G.add(a, G.mul(G.norm(G.sub(c, a)), min(40.0, 0.5 * G.length(G.sub(c, a)))))
        d = G.length(G.sub(c, a))
        ti = tf + d / ROCKET
        f0, f1 = int(round(tf * RATE)), int(round(ti * RATE))
        if f1 <= f0:
            f1 = f0 + 1
        tr = len(s.tracks)
        s.tracks.append(dict(keys=[a + (0.0,), c + (0.0,)], closed=False, length=d))
        r = I("instances", model=m_rocket, kind=dbs.IK_TRACK, track=tr, f0=f0, f1=f1, p=(0, ROCKET, 0, 0, 1))
        I("emitters", part=p_trail, inst=r, pos=(0, 0, -10), dir=(0, 0, -1), rate=45, f0=f0, f1=f1, seed=n)
        I("emitters", part=p_tail, inst=r, pos=(0, 0, -9), dir=(0, 0, -1), rate=30, f0=f0, f1=f1,
          flags=dbs.EF_MOVE, seed=n + 1000)
        explode(f1, c, 1.6 if kind == "box" else 1.0, seed=n)
        hit_at[k] = (f0, f1)
    # ---- Monsters and boxes ------------------------------------------------------------------
    for k, (kind, floor, yaw, c) in enumerate(targets):
        fire, hit = hit_at.get(k, (FRAMES, FRAMES))
        if kind == "box":
            I("instances", model=m_crate, p=floor + (0, 0, 0, 1), f1=hit if hit < FRAMES else 0)
            continue
        model, fmap, lay = (m_soldier, fmap_s, SOLDIER) if kind == "soldier" else (m_dog, fmap_d, DOG)
        phase = rng.uniform(0, 1)
        if hit >= FRAMES:                           # never shot: stands its ground
            I("instances", model=model, p=floor + (yaw, 0, 0, 1), anim=anim(fmap, "stand", lay, phase=phase))
            continue
        eye, _ = player(hit / RATE)
        face = math.degrees(math.atan2(eye[0] - floor[0], eye[2] - floor[2]))
        react = max(0, fire - 30)                   # it notices the player a little before the shot
        I("instances", model=model, p=floor + (yaw, 0, 0, 1), f1=react, anim=anim(fmap, "stand", lay, phase=phase))
        end = floor
        if kind == "dog" and hit - react >= 5:      # runs at the player, as far as the rocket lets it
            run = min(160.0, 320.0 * (hit - react) / RATE)
            dirn = G.norm((eye[0] - floor[0], 0, eye[2] - floor[2]))
            end = G.add(floor, G.mul(dirn, run))
            tr = len(s.tracks)
            s.tracks.append(dict(keys=[floor + (0.0,), end + (0.0,)], closed=False, length=run))
            I("instances", model=model, kind=dbs.IK_TRACK, track=tr, f0=react, f1=hit,
              p=(0, run / max(1e-3, (hit - react) / RATE), 0, 0, 1), anim=anim(fmap, "run", lay, 15.0))
        else:
            I("instances", model=model, p=floor + (face, 0, 0, 1), f0=react, f1=hit,
              anim=anim(fmap, "shoot" if kind == "soldier" else "stand", lay))
        a, n = lay["death"]
        dur = int(round(n / 10.0 * RATE))
        I("instances", model=model, p=end + (face, 0, 0, 1), f0=hit, f1=hit + dur, anim=(fmap[a], n, 10.0, 0))
        gone = 0                                    # the corpse goes when the player can no longer see it
        for f in range(hit + dur + 50, FRAMES, 5):
            eye, fwd = player(f / RATE)
            if G.dot(fwd, G.sub(end, eye)) < 0 or not visible(eye, G.add(end, (0, 16, 0))):
                gone = f
                break
        I("instances", model=model, p=end + (face, 0, 0, 1), f0=hit + dur, f1=gone,
          anim=(fmap[a + n - 1], 1, 1, 0))
    # ---- The launcher in hand, kicking at each shot (first-person shots only) -------------------
    fires = [int(round(tf * RATE)) for tf, _ in shots]
    view_shots = [(0, 260), (380, 640), (760, FRAMES)]
    for v0, v1 in view_shots:
        cuts = [v0] + [f for f in fires if v0 < f < v1] + [v1]
        for a, b in zip(cuts, cuts[1:]):
            kick = min(b, a + 6) if a in fires else a  # frames 5-7: the kick's last part (2-4 pass behind the eye)
            if kick > a:
                I("instances", model=m_launcher, flags=dbs.IF_VIEW | dbs.IF_NOCULL, f0=a, f1=kick,
                  p=(0, 0, 0, 180, 0, 0, 1), anim=(5, 3, 12.5, 0))
            if b > kick:
                I("instances", model=m_launcher, flags=dbs.IF_VIEW | dbs.IF_NOCULL, f0=kick, f1=b,
                  p=(0, 0, 0, 180, 0, 0, 1), anim=(0, 1, 1, 0))
    # ---- Embers over every other torch; water and lava move -------------------------------------
    sprite = [b for b in s.batches if b["flags"] & dbs.BF_SPRITE]
    if sprite:
        b = sprite[0]
        for n, v in enumerate(s.verts[b["vfirst"]:b["vfirst"] + b["vcount"]]):
            if n % 2 == 0:
                I("emitters", part=p_ember, pos=(v[0], v[1] + v[8] * 0.3, v[2]), dir=(0, 1, 0), rate=3,
                  seed=5000 + n)
    for i, b in enumerate(s.batches):
        if b["tex"] in names and names[b["tex"]].startswith("*"):
            I("surfaces", batch=i, kind=dbs.SK_WARP, p=(0.12, 4.0, 1.6))
    # ---- Shots: the walk, two cut-aways -------------------------------------------------------------
    look = 170.0

    def walk(f0, f1):
        return dict(f0=f0, f1=f1, kind=dbs.CK_PATH, track=path, p=(speed * f0 / RATE, speed, 0, look))

    def blocked(a, b):
        """A crate (an instance: the BSP trace cannot see it) within 40 units of the line a-b."""
        ab = G.sub(b, a)
        for kind, floor, yaw, c in targets:
            if kind == "box":
                t = G.clamp(G.dot(G.sub(c, a), ab) / max(1e-6, G.dot(ab, ab)))
                if G.length(G.sub(c, G.add(a, G.mul(ab, t)))) < 40:
                    return True
        return False

    def cutaway(f0, f1):
        """A fixed camera off the walk, watching the first kill in the window, in clear sight of it."""
        for tf, k in shots:
            f = int(tf * RATE)
            if f0 + 15 <= f <= f1 - 40 and targets[k][0] != "box":
                c = targets[k][3]
                for back in (260.0, 200.0, 150.0):
                    for ang in range(0, 360, 30):
                        p = G.add(c, (back * math.sin(math.radians(ang)), 70, back * math.cos(math.radians(ang))))
                        if world.contents(y2q(p)) == bsp.CONTENTS_EMPTY and visible(p, c) and \
                                visible(p, player(f / RATE)[0]) and not blocked(p, c):
                            return dict(f0=f0, f1=f1, kind=dbs.CK_FIXED, fov=65, p=p + (0.0,) + c)
        return walk(f0, f1)
    s.shots += [walk(0, 260), cutaway(260, 380), walk(380, 640), cutaway(640, 760), walk(760, FRAMES)]
    # The frame saved for image checks: in a walk, the most monsters in clear sight, and no rocket or
    # explosion near it in time (blended smoke and fire is where cards may rightly differ).
    best, capture = -1, 120
    for v0, v1 in view_shots:
        for f in range(v0 + 10, v1 - 10, 5):
            eye, fwd = player(f / RATE)
            if any(f - 30 < x <= f + 2 for x in fires) or \
                    any(f - 50 < b["frame"] <= f and G.length(G.sub(b["pos"], eye)) < 450 for b in s.bursts):
                continue
            n = 0
            for x in s.instances:
                if x["model"] in (m_soldier, m_dog) and x.get("f0", 0) <= f < (x.get("f1", 0) or FRAMES):
                    c = G.add(tuple(x["p"][:3]), (0, 26, 0))
                    if G.dot(fwd, G.sub(c, eye)) > 0 and G.length(G.sub(c, eye)) < 700 and visible(eye, c):
                        n += 1
            if n > best:
                best, capture = n, f
    s.game = dict(frames=FRAMES, rate=RATE, clear_rgb=0, flags=dbs.GF_QSKY, fovy=70.0, znear=2.0, zfar=4096.0,
                  sun=SUN, sun_rgb=(0.6, 0.55, 0.5), ambient=(0.45, 0.42, 0.4), seed=4242, sky=dbs.NONE,
                  capture=capture)
    s.info.update(map="maps/lq_e0m1.bsp", shots=str(len(shots)))
    print("arena: %d targets, %d rockets, path %.0f units at %.0f units/s, capture frame %d (%d in sight)" % (
        len(targets), len(shots), s.tracks[path]["length"], speed, capture, best))
    G.budget(s, "ARENA")
    return s


if __name__ == "__main__":
    import sys
    build().write(sys.argv[1] if len(sys.argv) > 1 else "ARENA.DBS")
