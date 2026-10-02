#!/usr/bin/env python3
"""The .DBS scene format (docs/content.md), PNG decoding and texture resampling.

A .DBS file is little-endian: a 16-byte header ('DBS1', version, section
count, 0) and sections (4-byte tag, 32-bit length, payload padded to 4):

  TEXS  u32 n; per texture: u16 w, u16 h, u16 flags, u16 0, RGBA8 texels
        (w, h powers of two <= 256, aspect <= 8; flags 1 mipmap, 2 clamp,
        4 lightmap)
  VERT  u32 n; 32-byte vertices: f32 x y z, u8 r g b a, f32 u v, f32 u2 v2
  INDX  u32 n; u16 indices (relative to the batch's first vertex)
  BTCH  u32 n; 48-byte batches: u16 tex, u16 lightmap (0xFFFF none),
        u16 flags (1 sky, 2 translucent, 4 alpha-tested, 8 two-sided,
        16 sprite), u16 leaf, u32 vfirst vcount ifirst icount,
        f32 mins[3] maxs[3]
  VIEW  u32 kind (0 orbit, 1 path), u32 frames, f32 znear zfar,
        u32 fog_rgb, f32 fog_start fog_end, u32 nkeys, then
        orbit: f32 centre[3], radius, height; path: nkeys x f32 pos[3] look[3]
  VISL  (levels) BSP nodes, leaves and visibility; see tools/bsp.py
  INFO  UTF-8 text, key=value lines (name, source, licence, credit)

Version 2 (game scenes: written only when a scene has a GHDR; every other
file stays version 1, byte for byte) adds, after BTCH, in this order (u32
count first, records as in src/core/scene.h):

  NRML  u32 n (<= 256), f32 normals[n][3]; u32 nvert, u8 index per vertex
  GHDR  frames, f32 rate (story frames/s), clear_rgb, flags (1 fog, 2 Quake sky), f32
        fovy znear zfar, fog_rgb, f32 fog_start fog_end, f32 sun[3]
        (towards it), sun_rgb[3], ambient[3], seed, sky model, capture frame
  MODL  32-byte models: u16 batch0 nbatch flags (1 lit: lit on the CPU
        through NRML; 2 animated: VANM) lod_next, f32 lod_dist, centre[3],
        radius, u16 anim, 0
  VANM  per animation: u32 nverts nframes vfirst, f32 scale[3] origin[3],
        u8 (x, y, z, normal) per vertex per frame (Quake MDL's packing)
  TRAK  per track: u32 nkeys flags (1 closed), f32 length, then f32 (x, y,
        z, roll degrees) per key, keys evenly spaced along the track
  INST  80-byte instances: u16 model, u8 kind, u8 flags (1 face the way
        it goes, 2 never culled, 4 placed in view space), u16 parent track, u32 f0 f1 (there for
        f0 <= f < f1; f1 0: to the end), f32 p[12], f32 anim[4] (first
        frame, frames, frames/s, phase s). Kinds and p, with tau the
        instance's own time ((f - f0) / rate):
          0 static  pos[3], yaw pitch roll (degrees), scale
          1 track   start (m), speed (m/s), lateral (m, + left), height,
                    scale, surge (m), surge Hz, surge phase (rad), yaw,
                    acceleration (m/s^2)
          2 spin    as static, then axis[3] (model space), degrees/s, phase
          3 orbit   centre[3], radius, degrees/s, phase, tilt (degrees,
                    about x), scale, yaw
  PART  64-byte particle kinds: u16 tex flags (1 add, 2 flat: lies in
        x-z, 4 alpha-tested, 8 no fog), f32 life jitter size0 size1 speed
        speed_jitter spread gravity drag rise, u32 rgba0 rgba1, f32 spin
        fade_in, 0
  EMIT  48-byte emitters: u16 part inst, u32 f0 f1, f32 pos[3] dir[3]
        rate, u32 seed flags (1 particles move with the instance)
  FXEV  32-byte bursts: u32 frame, u16 part inst, f32 pos[3], u32 count
        seed, f32 scale
  CAMS  64-byte shots: u32 f0 f1, u16 kind target track look_track, f32
        fov, p[11]; tau = (f - f0) / rate. Kinds:
          0 path    eye on track at p0 + p1 tau (m), height p2; looks at
                    target, else at look_track at p3 + p4 tau, height p5,
                    else p3 metres ahead on its own track
          1 chase   behind target: back p0, up p1, look ahead p2, look up
                    p3, left p4
          2 fixed   eye p0-p2; looks at target (up p3), else at p4-p6
          3 mount   on target at p0-p2 (model space), looking along p3-p5
          4 orbit   around target (else p0-p2): radius p3, height p4,
                    degrees/s p5, phase p6, look up p7
  SURF  32-byte batch effects: u16 batch kind, f32 p[7]:
          0 scroll  u, v per second
          1 warp    amplitude, frequency, speed (Quake water)
          2 ramp    lights switching on at the vertex's u2 seconds and off
                    at v2 (0: never), fading over p0 seconds
          3 pulse   colour x (p0 + p1 wave(p2 t + u2)); p3 1: flicker

New batch flags: 32 additive, 64 glow (additive, never fogged), 128 decal.

Standard library only.
"""
import struct
import zlib

MAGIC = b"DBS1"
VERSION = 1
TF_MIPMAP, TF_CLAMP, TF_LIGHTMAP = 1, 2, 4
BF_SKY, BF_TRANS, BF_ALPHATEST, BF_TWOSIDED, BF_SPRITE = 1, 2, 4, 8, 16
BF_ADD, BF_GLOW, BF_DECAL = 32, 64, 128
NONE = 0xFFFF
VERT = struct.Struct("<3f4B4f")
BATCH = struct.Struct("<4H4I6f")
assert VERT.size == 32 and BATCH.size == 48

# Version 2 records (src/core/scene.h).
GF_FOG, GF_QSKY = 1, 2
MF_LIT, MF_ANIM = 1, 2
IK_STATIC, IK_TRACK, IK_SPIN, IK_ORBIT = range(4)
IF_FACE, IF_NOCULL, IF_VIEW = 1, 2, 4
PF_ADD, PF_FLAT, PF_ALPHATEST, PF_NOFOG = 1, 2, 4, 8
EF_MOVE = 1
CK_PATH, CK_CHASE, CK_FIXED, CK_MOUNT, CK_ORBIT = range(5)
SK_SCROLL, SK_WARP, SK_RAMP, SK_PULSE = range(4)
GHDR = struct.Struct("<If2I3fI2f9fIII")
MODL = struct.Struct("<4Hf4f2H")
INST = struct.Struct("<H2B2H2I12f4f")
PART = struct.Struct("<2H10f2I2fI")
EMIT = struct.Struct("<2H2I7f2I")
FXEV = struct.Struct("<I2H3f2If")
CAMS = struct.Struct("<2I4Hf11f")
SURF = struct.Struct("<2H7f")
assert (GHDR.size, MODL.size, INST.size, PART.size, EMIT.size, FXEV.size, CAMS.size, SURF.size) == \
    (88, 32, 80, 64, 48, 32, 64, 32)
TEX_BUDGET = (2 << 20) - (128 << 10)    # the 2 MB Glide TMU, less the overlay font and slack
UV_LIMIT = 32.0


def sphere_normals(n=256):
    """n directions spread evenly over the sphere (a Fibonacci spiral): NRML's table."""
    import math
    out, ga = [], math.pi * (3 - math.sqrt(5))
    for i in range(n):
        y = 1 - 2 * (i + 0.5) / n
        r = math.sqrt(max(0.0, 1 - y * y))
        out.append((r * math.cos(ga * i), y, r * math.sin(ga * i)))
    return out


def nearest_normal(table, v):
    best, bi = -2.0, 0
    for i, t in enumerate(table):
        d = t[0] * v[0] + t[1] * v[1] + t[2] * v[2]
        if d > best:
            best, bi = d, i
    return bi


class Scene:
    def __init__(self):
        self.textures = []          # (w, h, flags, rgba bytes)
        self.verts = []             # (x, y, z, r, g, b, a, u, v, u2, v2)
        self.indices = []
        self.batches = []           # dict(tex, lm, flags, leaf, vfirst, vcount, ifirst, icount, mins, maxs)
        self.view = None            # dict
        self.vis = b""
        self.info = {}
        # Version 2: set game (GHDR's fields) to write a game scene.
        self.game = None
        self.normals = {}           # vertex index -> (nx, ny, nz), for lit models
        self.models, self.anims, self.tracks, self.instances = [], [], [], []
        self.parts, self.emitters, self.bursts, self.shots, self.surfaces = [], [], [], [], []

    def add_texture(self, w, h, rgba, flags=TF_MIPMAP):
        assert w & (w - 1) == 0 and h & (h - 1) == 0 and max(w, h) <= 256 and max(w, h) // min(w, h) <= 8
        assert len(rgba) == w * h * 4
        self.textures.append((w, h, flags, bytes(rgba)))
        return len(self.textures) - 1

    def add_batch(self, verts, tris, tex=NONE, lm=NONE, flags=0, leaf=NONE):
        """verts: list of vertex tuples; tris: list of index triples into verts."""
        assert len(verts) <= 65536
        vfirst, ifirst = len(self.verts), len(self.indices)
        self.verts.extend(verts)
        for t in tris:
            self.indices.extend(t)
        xs = [v[0] for v in verts] or [0]
        ys = [v[1] for v in verts] or [0]
        zs = [v[2] for v in verts] or [0]
        self.batches.append(dict(tex=tex, lm=lm, flags=flags, leaf=leaf, vfirst=vfirst, vcount=len(verts),
                                 ifirst=ifirst, icount=len(tris) * 3,
                                 mins=(min(xs), min(ys), min(zs)), maxs=(max(xs), max(ys), max(zs))))

    def set_normals(self, vfirst, normals):
        """Normals for vertices vfirst.. (a lit model's), as (nx, ny, nz)."""
        for i, n in enumerate(normals):
            self.normals[vfirst + i] = n

    def add_model(self, batch0, nbatch, flags=0, lod_next=NONE, lod_dist=0.0, anim=0):
        """A model: batches batch0..batch0+nbatch-1; its bounding sphere from their vertices."""
        pts = [self.verts[i] for b in self.batches[batch0:batch0 + nbatch]
               for i in range(b["vfirst"], b["vfirst"] + b["vcount"])]
        lo = [min(p[k] for p in pts) for k in range(3)] if pts else [0, 0, 0]
        hi = [max(p[k] for p in pts) for k in range(3)] if pts else [0, 0, 0]
        c = [(lo[k] + hi[k]) / 2 for k in range(3)]
        r = max([sum((p[k] - c[k]) ** 2 for k in range(3)) ** 0.5 for p in pts] or [0.0])
        self.models.append(dict(batch0=batch0, nbatch=nbatch, flags=flags, lod_next=lod_next, lod_dist=lod_dist,
                                centre=c, radius=r, anim=anim))
        return len(self.models) - 1

    def add(self, what, **kw):
        """Append a record to instances, parts, emitters, bursts, shots or surfaces; returns its index."""
        lst = getattr(self, what)
        lst.append(kw)
        return len(lst) - 1

    def triangles(self):
        return len(self.indices) // 3

    def texture_bytes16(self):
        """Texture memory at 16 bits per texel with full mip chains (the Glide budget)."""
        total = 0
        for w, h, flags, _ in self.textures:
            n = w * h * 2
            total += n * 4 // 3 if flags & TF_MIPMAP else n
        return total

    def write(self, path):
        secs = []
        t = bytearray(struct.pack("<I", len(self.textures)))
        for w, h, flags, rgba in self.textures:
            t += struct.pack("<4H", w, h, flags, 0) + rgba
        secs.append((b"TEXS", bytes(t)))
        v = bytearray(struct.pack("<I", len(self.verts)))
        for x in self.verts:
            v += VERT.pack(x[0], x[1], x[2], *[max(0, min(255, int(c))) for c in x[3:7]], *x[7:11])
        secs.append((b"VERT", bytes(v)))
        secs.append((b"INDX", struct.pack("<I", len(self.indices)) + struct.pack("<%dH" % len(self.indices),
                                                                                  *self.indices)))
        b = bytearray(struct.pack("<I", len(self.batches)))
        for x in self.batches:
            b += BATCH.pack(x["tex"], x["lm"], x["flags"], x["leaf"], x["vfirst"], x["vcount"], x["ifirst"],
                            x["icount"], *x["mins"], *x["maxs"])
        secs.append((b"BTCH", bytes(b)))
        if self.game is not None:
            secs += self.v2_sections()
        if self.view:
            secs.append((b"VIEW", pack_view(self.view)))
        if self.vis:
            secs.append((b"VISL", self.vis))
        secs.append((b"INFO", "".join("%s=%s\n" % kv for kv in self.info.items()).encode("utf-8")))
        out = bytearray(MAGIC + struct.pack("<3I", 2 if self.game is not None else VERSION, len(secs), 0))
        for tag, data in secs:
            out += tag + struct.pack("<I", len(data)) + data + b"\0" * (-len(data) % 4)
        open(path, "wb").write(out)
        return len(out)


    def v2_sections(self):
        g = dict(frames=1000, rate=25.0, clear_rgb=0, flags=0, fovy=60.0, znear=1.0, zfar=1000.0, fog_rgb=0,
                 fog_start=0.0, fog_end=0.0, sun=(0.0, 1.0, 0.0), sun_rgb=(1.0, 1.0, 1.0), ambient=(0.3, 0.3, 0.3),
                 seed=1, sky=NONE, capture=None)
        g.update(self.game)
        cap = g["capture"] if g["capture"] is not None else g["frames"] // 2
        secs = []
        if self.normals:
            table = sphere_normals()
            idx = bytes(nearest_normal(table, self.normals[i]) if i in self.normals else 0
                        for i in range(len(self.verts)))
            secs.append((b"NRML", struct.pack("<I", len(table)) + b"".join(struct.pack("<3f", *t) for t in table) +
                         struct.pack("<I", len(idx)) + idx))
        secs.append((b"GHDR", GHDR.pack(g["frames"], g["rate"], g["clear_rgb"], g["flags"], g["fovy"], g["znear"],
                                        g["zfar"], g["fog_rgb"], g["fog_start"], g["fog_end"], *g["sun"],
                                        *g["sun_rgb"], *g["ambient"], g["seed"], g["sky"], cap)))
        secs.append((b"MODL", struct.pack("<I", len(self.models)) + b"".join(
            MODL.pack(m["batch0"], m["nbatch"], m["flags"], m["lod_next"], m["lod_dist"], *m["centre"], m["radius"],
                      m["anim"], 0) for m in self.models)))
        if self.anims:
            a = bytearray(struct.pack("<I", len(self.anims)))
            for x in self.anims:
                assert len(x["data"]) == x["nframes"] * x["nverts"] * 4
                a += struct.pack("<3I6f", x["nverts"], x["nframes"], x["vfirst"], *x["scale"], *x["origin"]) + x["data"]
            secs.append((b"VANM", bytes(a)))
        if self.tracks:
            t = bytearray(struct.pack("<I", len(self.tracks)))
            for x in self.tracks:
                t += struct.pack("<2If", len(x["keys"]), 1 if x.get("closed") else 0, x["length"])
                t += b"".join(struct.pack("<4f", *k) for k in x["keys"])
            secs.append((b"TRAK", bytes(t)))

        def fl(v, n):
            v = list(v) + [0.0] * n
            return v[:n]
        secs.append((b"INST", struct.pack("<I", len(self.instances)) + b"".join(
            INST.pack(i["model"], i.get("kind", IK_STATIC), i.get("flags", 0), i.get("parent", NONE),
                      i.get("track", NONE), i.get("f0", 0), i.get("f1", 0), *fl(i.get("p", ()), 12),
                      *fl(i.get("anim", ()), 4)) for i in self.instances)))
        if self.parts:
            secs.append((b"PART", struct.pack("<I", len(self.parts)) + b"".join(
                PART.pack(x.get("tex", NONE), x.get("flags", 0), x["life"], x.get("life_jitter", 0.0), x["size0"],
                          x.get("size1", x["size0"]), x.get("speed", 0.0), x.get("speed_jitter", 0.0),
                          x.get("spread", 0.0), x.get("gravity", 0.0), x.get("drag", 0.0), x.get("rise", 0.0),
                          x.get("rgba0", 0xFFFFFFFF), x.get("rgba1", x.get("rgba0", 0xFFFFFFFF)),
                          x.get("spin", 0.0), x.get("fade_in", 0.0), 0) for x in self.parts)))
        if self.emitters:
            secs.append((b"EMIT", struct.pack("<I", len(self.emitters)) + b"".join(
                EMIT.pack(x["part"], x.get("inst", NONE), x.get("f0", 0), x.get("f1", 0), *x.get("pos", (0, 0, 0)),
                          *x.get("dir", (0, 1, 0)), x["rate"], x.get("seed", 0), x.get("flags", 0))
                for x in self.emitters)))
        if self.bursts:
            secs.append((b"FXEV", struct.pack("<I", len(self.bursts)) + b"".join(
                FXEV.pack(x["frame"], x["part"], x.get("inst", NONE), *x.get("pos", (0, 0, 0)), x["count"],
                          x.get("seed", 0), x.get("scale", 1.0)) for x in self.bursts)))
        secs.append((b"CAMS", struct.pack("<I", len(self.shots)) + b"".join(
            CAMS.pack(x["f0"], x["f1"], x["kind"], x.get("target", NONE), x.get("track", NONE),
                      x.get("look_track", NONE), x.get("fov", 0.0), *fl(x.get("p", ()), 11)) for x in self.shots)))
        if self.surfaces:
            secs.append((b"SURF", struct.pack("<I", len(self.surfaces)) + b"".join(
                SURF.pack(x["batch"], x["kind"], *fl(x.get("p", ()), 7)) for x in self.surfaces)))
        return secs

    def check(self):
        """Problems that would make a game scene fail or misbehave on a card, as text."""
        probs = []
        if self.texture_bytes16() > TEX_BUDGET:
            probs.append("textures need %d KB at 16 bits, over the %d KB TMU" %
                         (self.texture_bytes16() // 1024, TEX_BUDGET // 1024))
        # Generated models keep texture coordinates within UV_LIMIT; a converted Quake world
        # (batches outside every model) keeps the map's own, which the fly-through draws everywhere.
        in_model = set()
        for m in self.models:
            in_model.update(range(m["batch0"], m["batch0"] + m["nbatch"]))
        if not self.models:
            in_model = set(range(len(self.batches)))
        def coords(b):                              # u2, v2 are a lightmap's only with one (ramps keep times there)
            return slice(7, 11) if b["lm"] != NONE else slice(7, 9)
        big = max([abs(c) for i in in_model for v in self.verts[self.batches[i]["vfirst"]:self.batches[i]["vfirst"] +
                                                                  self.batches[i]["vcount"]]
                   for c in v[coords(self.batches[i])]] or [0])
        if big > UV_LIMIT:
            probs.append("a texture coordinate reaches %.1f (limit %g)" % (big, UV_LIMIT))
        nb, nm, ni, nt = len(self.batches), len(self.models), len(self.instances), len(self.tracks)
        for i, b in enumerate(self.batches):
            if b["vcount"] > 65536 or (b["tex"] != NONE and b["tex"] >= len(self.textures)):
                probs.append("batch %d out of range" % i)
        if self.game is None:
            return probs
        sky = self.game.get("sky", NONE)
        if sky != NONE and sky >= nm:
            probs.append("sky model %d" % sky)
        for i, m in enumerate(self.models):
            if m["batch0"] + m["nbatch"] > nb or (m["lod_next"] != NONE and m["lod_next"] >= nm):
                probs.append("model %d out of range" % i)
            if m["flags"] & MF_LIT and not any(v in self.normals for b in self.batches[m["batch0"]:m["batch0"] + m["nbatch"]]
                                               for v in range(b["vfirst"], b["vfirst"] + b["vcount"])):
                probs.append("model %d is lit but has no normals" % i)
            if m["flags"] & MF_ANIM and m["anim"] >= len(self.anims):
                probs.append("model %d animation %d" % (i, m["anim"]))
        for i, x in enumerate(self.instances):
            par, tr = x.get("parent", NONE), x.get("track", NONE)
            if x["model"] >= nm or (par != NONE and par >= i) or (tr != NONE and tr >= nt) or \
                    (x.get("kind", 0) == IK_TRACK and tr == NONE):
                probs.append("instance %d out of range" % i)
        for i, x in enumerate(self.emitters):
            if x["part"] >= len(self.parts) or x.get("inst", NONE) not in (NONE,) + tuple(range(ni)):
                probs.append("emitter %d out of range" % i)
        for i, x in enumerate(self.bursts):
            if x["part"] >= len(self.parts) or x.get("inst", NONE) not in (NONE,) + tuple(range(ni)):
                probs.append("burst %d out of range" % i)
        frames = self.game.get("frames", 1000)
        covered = [False] * frames
        for i, x in enumerate(self.shots):
            for k in ("target", "track", "look_track"):
                lim = ni if k == "target" else nt
                if x.get(k, NONE) != NONE and x[k] >= lim:
                    probs.append("shot %d %s out of range" % (i, k))
            for f in range(max(0, x["f0"]), min(frames, x["f1"])):
                covered[f] = True
        if not all(covered):
            probs.append("frame %d has no shot" % covered.index(False))
        for i, x in enumerate(self.surfaces):
            if x["batch"] >= nb:
                probs.append("surface %d out of range" % i)
        return probs


def pack_view(v):
    keys = v.get("keys", [])
    head = struct.pack("<2I2fI2fI", v.get("kind", 0), v.get("frames", 360), v.get("znear", 1.0), v.get("zfar", 1000.0),
                       v.get("fog_rgb", 0), v.get("fog_start", 0.0), v.get("fog_end", 0.0), len(keys))
    if v.get("kind", 0) == 0:
        return head + struct.pack("<5f", *v["centre"], v["radius"], v["height"])
    return head + b"".join(struct.pack("<6f", *p, *l) for p, l in keys)


def read(path):
    """Parse a .DBS file into a dict of sections (for checks and tests)."""
    d = open(path, "rb").read()
    if d[:4] != MAGIC:
        raise ValueError("not a DBS file")
    ver, n, _ = struct.unpack_from("<3I", d, 4)
    off, secs = 16, {}
    for _ in range(n):
        tag, ln = d[off:off + 4].decode(), struct.unpack_from("<I", d, off + 4)[0]
        secs[tag] = d[off + 8:off + 8 + ln]
        off += 8 + ln + (-ln % 4)
    out = {"version": ver, "sections": sorted(secs)}
    if "TEXS" in secs:
        s, texs, o = secs["TEXS"], [], 4
        for _ in range(struct.unpack_from("<I", s)[0]):
            w, h, fl, _ = struct.unpack_from("<4H", s, o)
            texs.append((w, h, fl))
            o += 8 + w * h * 4
        out["textures"] = texs
    if "VERT" in secs:
        out["nverts"] = struct.unpack_from("<I", secs["VERT"])[0]
    if "INDX" in secs:
        out["nindices"] = struct.unpack_from("<I", secs["INDX"])[0]
    if "BTCH" in secs:
        s = secs["BTCH"]
        out["batches"] = [BATCH.unpack_from(s, 4 + i * 48) for i in range(struct.unpack_from("<I", s)[0])]
    if "INFO" in secs:
        out["info"] = dict(l.split("=", 1) for l in secs["INFO"].decode().splitlines() if "=" in l)
    if "GHDR" in secs:
        g = GHDR.unpack_from(secs["GHDR"])
        out["game"] = dict(frames=g[0], rate=g[1], fog=bool(g[3] & GF_FOG), sky=g[20], capture=g[21])
    for tag, rec in (("MODL", MODL), ("INST", INST), ("PART", PART), ("EMIT", EMIT), ("FXEV", FXEV),
                     ("CAMS", CAMS), ("SURF", SURF)):
        if tag in secs:
            n = struct.unpack_from("<I", secs[tag])[0]
            out[tag.lower()] = [rec.unpack_from(secs[tag], 4 + i * rec.size) for i in range(n)]
    for tag in ("TRAK", "VANM"):
        if tag in secs:
            out["n" + tag.lower()] = struct.unpack_from("<I", secs[tag])[0]
    return out


# ---- PNG -------------------------------------------------------------------
def png_decode(data):
    """Decode a non-interlaced 8-bit PNG (grey, RGB, palette, grey+alpha, RGBA) to (w, h, RGBA bytes)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    off, idat, plte, trns = 8, bytearray(), None, None
    while off < len(data):
        ln, typ = struct.unpack_from(">I4s", data, off)
        body = data[off + 8:off + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        off += 12 + ln
    if depth != 8 or interlace:
        raise ValueError("PNG depth %d interlace %d not supported" % (depth, interlace))
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(bytes(idat))
    stride = w * ch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    o = 0
    for y in range(h):
        f = raw[o]
        line = bytearray(raw[o + 1:o + 1 + stride])
        o += 1 + stride
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                left = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    rgba = bytearray(w * h * 4)
    if ctype == 6:
        rgba[:] = out
    elif ctype == 2:
        rgba[0::4], rgba[1::4], rgba[2::4] = out[0::3], out[1::3], out[2::3]
        rgba[3::4] = b"\xff" * (w * h)
    elif ctype == 0:
        rgba[0::4] = rgba[1::4] = rgba[2::4] = out
        rgba[3::4] = b"\xff" * (w * h)
    elif ctype == 4:
        rgba[0::4] = rgba[1::4] = rgba[2::4] = out[0::2]
        rgba[3::4] = out[1::2]
    else:
        for i, p in enumerate(out):
            rgba[i * 4:i * 4 + 3] = plte[p * 3:p * 3 + 3]
            rgba[i * 4 + 3] = trns[p] if trns and p < len(trns) else 255
    return w, h, bytes(rgba)


def png_encode(w, h, rgba):
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


# ---- Resampling ------------------------------------------------------------
def pow2_at_most(v, limit=256):
    p = 1
    while p * 2 <= min(v, limit):
        p *= 2
    return p


def resize_box(w, h, rgba, nw, nh):
    """Area-average resample to nw x nh (downscaling; upscaling repeats texels)."""
    if (w, h) == (nw, nh):
        return bytes(rgba)
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        y0, y1 = y * h // nh, max(y * h // nh + 1, (y + 1) * h // nh)
        for x in range(nw):
            x0, x1 = x * w // nw, max(x * w // nw + 1, (x + 1) * w // nw)
            acc = [0, 0, 0, 0]
            n = 0
            for yy in range(y0, y1):
                row = yy * w * 4
                for xx in range(x0, x1):
                    i = row + xx * 4
                    acc[0] += rgba[i]; acc[1] += rgba[i + 1]; acc[2] += rgba[i + 2]; acc[3] += rgba[i + 3]
                    n += 1
            o = (y * nw + x) * 4
            out[o:o + 4] = bytes(((acc[0] + n // 2) // n, (acc[1] + n // 2) // n, (acc[2] + n // 2) // n,
                                  (acc[3] + n // 2) // n))
    return bytes(out)


def halve(w, h, rgba):
    """Fast exact 2x box reduction (w, h even)."""
    nw, nh = w // 2, h // 2
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        r0, r1 = (2 * y) * w * 4, (2 * y + 1) * w * 4
        for x in range(nw):
            i, j = r0 + 8 * x, r1 + 8 * x
            o = (y * nw + x) * 4
            for k in range(4):
                out[o + k] = (rgba[i + k] + rgba[i + 4 + k] + rgba[j + k] + rgba[j + 4 + k] + 2) >> 2
    return nw, nh, bytes(out)


def fit_texture(w, h, rgba, limit=256):
    """Power-of-two sides <= limit and aspect <= 8, by halving then area resampling."""
    while (w > 2 * limit or h > 2 * limit) and w % 2 == 0 and h % 2 == 0:
        w, h, rgba = halve(w, h, rgba)
    nw, nh = pow2_at_most(w, limit), pow2_at_most(h, limit)
    while nw > 8 * nh:
        nw //= 2
    while nh > 8 * nw:
        nh //= 2
    return nw, nh, resize_box(w, h, rgba, nw, nh)


def selftest(path):
    """The fixture tests/unit/test_scene.c reads."""
    s = Scene()
    s.add_texture(4, 2, bytes(range(32)), TF_MIPMAP | TF_CLAMP)
    v = [(0, 0, 0, 255, 0, 0, 255, 0, 0, 0, 0), (1, 0, 0, 0, 255, 0, 255, 1, 0, 1, 0), (0, 1, 0, 0, 0, 255, 128, 0, 1, 0, 1)]
    s.add_batch(v, [(0, 1, 2)], tex=0)
    s.add_batch(v + [(1, 1, -2, 9, 9, 9, 9, 1, 1, 1, 1)], [(0, 1, 2), (1, 3, 2)], flags=BF_TWOSIDED)
    s.view = dict(kind=0, frames=4, znear=1.0, zfar=50.0, centre=(1.0, 2.0, 3.0), radius=10.0, height=5.0)
    s.info = {"name": "SELFTEST", "licence": "CC0-1.0"}
    return s.write(path)


def _box(s, size, rgb, tex, flags=0, normals=False):
    """A box batch centred on the origin (counter-clockwise outside); returns its batch index."""
    h = size / 2.0
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    verts, tris, ns = [], [], []
    for n, a, b in faces:
        base = len(verts)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = [h * (n[k] + su * a[k] + sv * b[k]) for k in range(3)]
            verts.append((p[0], p[1], p[2], rgb[0], rgb[1], rgb[2], 255, (su + 1) / 2, (sv + 1) / 2, 0, 0))
            ns.append(n)
        tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    vfirst = len(s.verts)
    s.add_batch(verts, tris, tex=tex, flags=flags)
    if normals:
        s.set_normals(vfirst, ns)
    return len(s.batches) - 1


def _quad(s, w, h, rgb, a, tex, flags=0, uv2=(0, 0)):
    verts = [(-w / 2, -h / 2, 0, rgb[0], rgb[1], rgb[2], a, 0, 0, uv2[0], uv2[1]),
             (w / 2, -h / 2, 0, rgb[0], rgb[1], rgb[2], a, 1, 0, uv2[0], uv2[1]),
             (w / 2, h / 2, 0, rgb[0], rgb[1], rgb[2], a, 1, 1, uv2[0], uv2[1]),
             (-w / 2, h / 2, 0, rgb[0], rgb[1], rgb[2], a, 0, 1, uv2[0], uv2[1])]
    s.add_batch(verts, [(0, 1, 2), (0, 2, 3)], tex=tex, flags=flags | BF_TWOSIDED)
    return len(s.batches) - 1


def selftest2(path):
    """A small version 2 scene using every feature (tests/unit/test_gs.c, make tests-host's replay)."""
    return selftest2_scene().write(path)


def selftest2_scene(args=None):
    """The selftest2 scene (also GSTEST.DBS, the B1SCN check: tools/assets.py)."""
    import math
    s = Scene()
    chk = bytes(c for y in range(8) for x in range(8) for c in ((220, 200, 90, 255) if (x ^ y) & 1 else (60, 90, 140, 255)))
    # Particle textures 32x32: smaller ones, magnified, show the runtimes' half-texel differences.
    fall = [max(0.0, 1 - math.hypot(x - 15.5, y - 15.5) / 15.5) for y in range(32) for x in range(32)]
    dot = bytes(c for f in fall for c in (255, 255, 255, int(255 * f)))             # blended: shape in alpha
    glow = bytes(c for f in fall for c in (int(255 * f * f),) * 3 + (255,))          # additive: shape in colour
    t_chk = s.add_texture(8, 8, chk)
    t_dot = s.add_texture(32, 32, dot, TF_CLAMP)
    t_glow = s.add_texture(32, 32, glow, TF_CLAMP)
    m_cube = s.add_model(_box(s, 2.0, (200, 200, 200), t_chk, normals=True), 1, flags=MF_LIT)
    m_small = s.add_model(_box(s, 1.0, (255, 0, 0), t_chk), 1)
    s.models[m_cube]["lod_next"], s.models[m_cube]["lod_dist"] = m_small, 40.0
    m_scroll = s.add_model(_quad(s, 4, 2, (255, 255, 255), 255, t_chk), 1)
    m_sky = s.add_model(_box(s, 400.0, (40, 60, 120), NONE, flags=BF_SKY | BF_TWOSIDED), 1)
    m_glow = s.add_model(_quad(s, 2, 2, (255, 200, 100), 255, t_glow, BF_GLOW), 1)
    m_add = s.add_model(_quad(s, 2, 2, (100, 200, 255), 255, t_glow, BF_ADD), 1)
    m_trans = s.add_model(_quad(s, 3, 3, (255, 255, 255), 128, t_chk, BF_TRANS), 1)
    m_decal = s.add_model(_quad(s, 1, 1, (0, 0, 0), 255, t_dot, BF_DECAL | BF_ALPHATEST), 1)
    m_ramp = s.add_model(_quad(s, 2, 1, (255, 230, 150), 255, NONE, BF_ADD, uv2=(1.0, 3.0)), 1)
    m_pulse = s.add_model(_quad(s, 1, 1, (255, 120, 40), 255, t_glow, BF_ADD), 1)
    m_warp = s.add_model(_quad(s, 6, 6, (80, 120, 200), 160, t_chk, BF_TRANS), 1)
    # A two-frame animated triangle.
    b = len(s.batches)
    s.add_batch([(0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0), (1, 0, 0, 255, 255, 255, 255, 1, 0, 0, 0),
                 (0, 1, 0, 255, 255, 255, 255, 0, 1, 0, 0)], [(0, 1, 2)], tex=t_chk, flags=BF_TWOSIDED)
    vf = s.batches[b]["vfirst"]
    s.anims.append(dict(nverts=3, nframes=2, vfirst=vf, scale=(0.02, 0.02, 0.02), origin=(0, 0, 0),
                        data=bytes([0, 0, 0, 1, 50, 0, 0, 1, 0, 50, 0, 1, 0, 0, 0, 2, 100, 0, 0, 2, 0, 100, 0, 2])))
    s.set_normals(vf, [(0, 0, 1)] * 3)
    m_anim = s.add_model(b, 1, flags=MF_ANIM | MF_LIT, anim=0)
    # A closed track: a circle of radius 20, banked.
    keys = [(20 * math.sin(2 * math.pi * k / 16), 1.0, 20 * math.cos(2 * math.pi * k / 16), 10.0) for k in range(16)]
    s.tracks.append(dict(keys=keys, closed=True, length=2 * math.pi * 20))
    s.game = dict(frames=100, rate=25.0, clear_rgb=0x203040, flags=GF_FOG, fovy=60.0, znear=0.5, zfar=500.0,
                  fog_rgb=0x405060, fog_start=20.0, fog_end=120.0, sun=(0.48, 0.8, 0.36), sun_rgb=(0.9, 0.85, 0.8),
                  ambient=(0.25, 0.25, 0.3), seed=7, sky=m_sky, capture=50)
    I = s.add
    i_static = I("instances", model=m_cube, p=(0, 1, 0, 30, 0, 0, 1))
    i_car = I("instances", model=m_cube, kind=IK_TRACK, track=0, p=(0, 8, 0, 1, 1, 2, 0.5, 0, 0, 0.5))
    I("instances", model=m_scroll, kind=IK_SPIN, parent=i_car, p=(0, 2, 0, 0, 0, 0, 1, 0, 1, 0, 90, 0))
    I("instances", model=m_cube, kind=IK_ORBIT, flags=IF_FACE, p=(0, 6, 0, 12, 30, 0, 20, 0.5))
    I("instances", model=m_cube, kind=IK_SPIN, p=(-6, 2, -6, 0, 0, 0, 1, 1, 1, 0, 45, 10))
    I("instances", model=m_cube, p=(0, 1, -90, 0, 0, 0, 1))            # far: drawn at its LOD
    I("instances", model=m_anim, p=(3, 0, 3, 0, 0, 0, 2), anim=(0, 2, 5, 0))
    I("instances", model=m_trans, p=(4, 2, 0, 90, 0, 0, 1))
    I("instances", model=m_warp, p=(0, 0.05, 0, 0, -90, 0, 1))
    I("instances", model=m_decal, p=(0, 1.01, 0, 0, -90, 0, 1), parent=i_static)
    I("instances", model=m_glow, p=(-4, 3, 0, 0, 0, 0, 1), f0=10, f1=80)
    I("instances", model=m_add, p=(-4, 5, 0, 0, 0, 0, 1))
    I("instances", model=m_ramp, p=(6, 3, -3, 0, 0, 0, 1))
    I("instances", model=m_pulse, p=(6, 5, -3, 0, 0, 0, 1), flags=IF_NOCULL)
    smoke = I("parts", tex=t_dot, life=1.5, life_jitter=0.3, size0=0.5, size1=2.0, speed=1.0, speed_jitter=0.5,
              spread=0.6, drag=0.5, rise=0.8, rgba0=0xB0B0B0A0, rgba1=0x60606000, spin=90, fade_in=0.1)
    spark = I("parts", tex=t_glow, flags=PF_ADD, life=0.8, size0=0.3, size1=0.1, speed=8, speed_jitter=0.5,
              spread=math.pi, gravity=9.8, rgba0=0xFFD080FF, rgba1=0xFF400000)
    ring = I("parts", tex=t_glow, flags=PF_ADD | PF_FLAT | PF_NOFOG, life=1.0, size0=1, size1=12,
             rgba0=0x80C0FFFF, rgba1=0x2040FF00)
    I("emitters", part=smoke, inst=i_car, pos=(0, 0.5, -1), dir=(0, 0.3, -1), rate=12, seed=3)
    I("emitters", part=spark, inst=i_static, pos=(0, 1, 0), dir=(0, 1, 0), rate=20, seed=4, flags=EF_MOVE,
      f0=30, f1=70)
    I("bursts", frame=40, part=spark, inst=i_car, count=40, seed=5, scale=1.5)
    I("bursts", frame=40, part=ring, pos=(0, 0.2, 0), count=1, seed=6)
    I("shots", f0=0, f1=20, kind=CK_PATH, track=0, p=(0, 6, 3, 15))
    I("shots", f0=20, f1=40, kind=CK_CHASE, target=i_car, p=(8, 3, 6, 0.5, 0))
    I("shots", f0=40, f1=60, kind=CK_FIXED, target=i_car, p=(25, 10, 25, 0.5), fov=45)
    I("shots", f0=60, f1=80, kind=CK_MOUNT, target=i_car, p=(0, 1.5, 0.5, 0.7, -0.1, 0.7))   # 45 deg in
    I("shots", f0=80, f1=100, kind=CK_ORBIT, p=(0, 0, 0, 30, 12, 36, 0, 1))
    I("surfaces", batch=s.models[m_scroll]["batch0"], kind=SK_SCROLL, p=(0.5, 0.25))
    I("surfaces", batch=s.models[m_warp]["batch0"], kind=SK_WARP, p=(0.1, 3.0, 2.0))
    I("surfaces", batch=s.models[m_ramp]["batch0"], kind=SK_RAMP, p=(0.5,))
    I("surfaces", batch=s.models[m_pulse]["batch0"], kind=SK_PULSE, p=(0.6, 0.4, 3.0, 1))
    s.info = {"name": "SELFTEST2", "licence": "CC0-1.0"}
    probs = s.check()
    assert not probs, probs
    return s


if __name__ == "__main__":
    import sys
    if sys.argv[1:2] == ["--selftest"]:
        selftest(sys.argv[2])
    elif sys.argv[1:2] == ["--selftest2"]:
        selftest2(sys.argv[2])
    else:
        for p in sys.argv[1:]:
            print(p, read(p))
