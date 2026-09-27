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

Standard library only.
"""
import struct
import zlib

MAGIC = b"DBS1"
VERSION = 1
TF_MIPMAP, TF_CLAMP, TF_LIGHTMAP = 1, 2, 4
BF_SKY, BF_TRANS, BF_ALPHATEST, BF_TWOSIDED, BF_SPRITE = 1, 2, 4, 8, 16
NONE = 0xFFFF
VERT = struct.Struct("<3f4B4f")
BATCH = struct.Struct("<4H4I6f")
assert VERT.size == 32 and BATCH.size == 48


class Scene:
    def __init__(self):
        self.textures = []          # (w, h, flags, rgba bytes)
        self.verts = []             # (x, y, z, r, g, b, a, u, v, u2, v2)
        self.indices = []
        self.batches = []           # dict(tex, lm, flags, leaf, vfirst, vcount, ifirst, icount, mins, maxs)
        self.view = None            # dict
        self.vis = b""
        self.info = {}

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
        if self.view:
            secs.append((b"VIEW", pack_view(self.view)))
        if self.vis:
            secs.append((b"VISL", self.vis))
        secs.append((b"INFO", "".join("%s=%s\n" % kv for kv in self.info.items()).encode("utf-8")))
        out = bytearray(MAGIC + struct.pack("<3I", VERSION, len(secs), 0))
        for tag, data in secs:
            out += tag + struct.pack("<I", len(data)) + data + b"\0" * (-len(data) % 4)
        open(path, "wb").write(out)
        return len(out)


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


if __name__ == "__main__":
    import sys
    if sys.argv[1:2] == ["--selftest"]:
        selftest(sys.argv[2])
    else:
        for p in sys.argv[1:]:
            print(p, read(p))
