"""Procedural textures for the game scenes: RGBA8 bytes, tileable where they
repeat, the same from the same seed everywhere. Additive textures carry their
shape in colour (black outside, alpha 255); blended ones carry it in alpha
(docs/content.md). Standard library only.
"""
import math

M32 = 0xFFFFFFFF


def hash3(x, y, seed):
    h = (x * 374761393 + y * 668265263 + seed * 2246822519) & M32
    h = ((h ^ (h >> 13)) * 1274126177) & M32
    return (h ^ (h >> 16)) & M32


def lattice(x, y, seed, period):
    return hash3(x % period, y % period, seed) / 4294967295.0


def value_noise(x, y, seed, period):
    """Smooth noise in [0, 1], tiling every period cells."""
    xi, yi = int(math.floor(x)), int(math.floor(y))
    fx, fy = x - xi, y - yi
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = lattice(xi, yi, seed, period), lattice(xi + 1, yi, seed, period)
    c, d = lattice(xi, yi + 1, seed, period), lattice(xi + 1, yi + 1, seed, period)
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


def fbm(u, v, seed, base=4, octaves=4, gain=0.5):
    """Fractal noise at texture coordinates (u, v) in [0, 1), tiling, about [0, 1]."""
    total, amp, norm_ = 0.0, 1.0, 0.0
    f = base
    for o in range(octaves):
        total += amp * value_noise(u * f, v * f, seed + o * 101, f)
        norm_ += amp
        amp *= gain
        f *= 2
    return total / norm_


def image(w, h, fn):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            r, g, b, a = fn(x / float(w), y / float(h), x, y)
            o = (y * w + x) * 4
            out[o] = max(0, min(255, int(r)))
            out[o + 1] = max(0, min(255, int(g)))
            out[o + 2] = max(0, min(255, int(b)))
            out[o + 3] = max(0, min(255, int(a)))
    return bytes(out)


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


# ---- Surfaces ----------------------------------------------------------------------
def plating(size, seed, base=(150, 155, 165), panels=4, dark=0.55):
    """Hull plates: panel seams, rivet-dark edges, grime, a few lighter plates."""
    def fn(u, v, x, y):
        n = fbm(u, v, seed, base=8, octaves=4)
        pu, pv = (u * panels) % 1.0, (v * panels * 2) % 1.0
        cell = hash3(int(u * panels), int(v * panels * 2), seed) / 4294967295.0
        seam = 1.0 if min(pu, 1 - pu) > 0.02 and min(pv, 1 - pv) > 0.03 else dark
        k = (0.75 + 0.35 * n) * seam * (0.9 + 0.2 * cell)
        return (base[0] * k, base[1] * k, base[2] * k, 255)
    return image(size, size, fn)


def rock(size, seed, base=(120, 108, 96)):
    def fn(u, v, x, y):
        n = fbm(u, v, seed, base=4, octaves=5, gain=0.55)
        c = fbm(u, v, seed + 7, base=16, octaves=2)
        k = 0.45 + 0.8 * n
        k *= 0.8 + 0.4 * c
        return (base[0] * k, base[1] * k, base[2] * k, 255)
    return image(size, size, fn)


def planet(w, h, seed):
    """Equirectangular gas-giant bands with storms."""
    def fn(u, v, x, y):
        warp = fbm(u, v, seed, base=4, octaves=3) * 0.15
        band = math.sin((v + warp) * 34.0) * 0.5 + 0.5
        n = fbm(u, v, seed + 3, base=8, octaves=4)
        c1, c2, c3 = (205, 170, 120), (150, 95, 60), (235, 215, 180)
        col = mix(mix(c2, c1, band), c3, max(0.0, n - 0.55) * 1.6)
        polar = 1 - 0.5 * (abs(v - 0.5) * 2) ** 3
        return (col[0] * polar, col[1] * polar, col[2] * polar, 255)
    return image(w, h, fn)


def stars(size, seed, density=0.004):
    """Black with stars (opaque: the sky's back layer)."""
    out = bytearray(size * size * 4)
    for i in range(size * size):
        out[i * 4 + 3] = 255
    n = int(size * size * density)
    for k in range(n):
        h = hash3(k, 17, seed)
        x, y = h % size, (h >> 12) % size
        b = 90 + (hash3(k, 29, seed) % 166)
        tint = hash3(k, 31, seed) % 3
        rgb = (b, b, min(255, b + 40)) if tint == 0 else (min(255, b + 30), b, b - 20 if b > 20 else b) if tint == 1 \
            else (b, b, b)
        o = (y * size + x) * 4
        out[o:o + 3] = bytes(max(0, min(255, c)) for c in rgb)
    return bytes(out)


def nebula(size, seed, tint=(120, 60, 160)):
    """An additive cloud: colour fades to black at the edges."""
    def fn(u, v, x, y):
        r = math.hypot(u - 0.5, v - 0.5) * 2
        fall = max(0.0, 1 - r) ** 1.5
        n = fbm(u, v, seed, base=3, octaves=5)
        k = fall * max(0.0, n - 0.3) * 2.2
        return (tint[0] * k, tint[1] * k, tint[2] * k, 255)
    return image(size, size, fn)


def windows(size, seed, lit=(255, 220, 150), frac=0.35):
    """Additive window lights: a grid of small lit rectangles on black."""
    def fn(u, v, x, y):
        cx, cy = int(u * 16), int(v * 32)
        on = hash3(cx, cy, seed) / 4294967295.0 < frac
        inside = 0.15 < (u * 16) % 1.0 < 0.85 and 0.2 < (v * 32) % 1.0 < 0.8
        k = 1.0 if on and inside else 0.0
        k *= 0.7 + 0.3 * (hash3(cx, cy, seed + 1) / 4294967295.0)
        return (lit[0] * k, lit[1] * k, lit[2] * k, 255)
    return image(size, size, fn)


# ---- Sprites (32x32 or more: docs/content.md) -------------------------------------
def glow(size, rgb=(255, 255, 255), power=2.0):
    """Additive: a soft round light."""
    def fn(u, v, x, y):
        r = math.hypot(u - 0.5 + 0.5 / size, v - 0.5 + 0.5 / size) * 2
        k = max(0.0, 1 - r) ** power
        return (rgb[0] * k, rgb[1] * k, rgb[2] * k, 255)
    return image(size, size, fn)


def fire(size, seed):
    """Additive: a lumpy fireball, white-yellow core to red edge."""
    def fn(u, v, x, y):
        r = math.hypot(u - 0.5, v - 0.5) * 2
        n = fbm(u, v, seed, base=4, octaves=3)
        k = max(0.0, 1 - r * (0.8 + 0.5 * n)) ** 1.2
        col = mix((200, 40, 0), (255, 230, 150), min(1.0, k * 1.6))
        return (col[0] * k, col[1] * k, col[2] * k, 255)
    return image(size, size, fn)


def smoke(size, seed, grey=200):
    """Blended: a soft lumpy puff, shape in alpha."""
    def fn(u, v, x, y):
        r = math.hypot(u - 0.5, v - 0.5) * 2
        n = fbm(u, v, seed, base=4, octaves=3)
        a = max(0.0, 1 - r) ** 1.3 * (0.6 + 0.6 * n)
        return (grey, grey, grey, 255 * min(1.0, a))
    return image(size, size, fn)


def ring(size, width=0.12):
    """Additive: a thin glowing ring (shockwaves)."""
    def fn(u, v, x, y):
        r = math.hypot(u - 0.5, v - 0.5) * 2
        k = max(0.0, 1 - abs(r - 0.85) / width) ** 2
        return (180 * k, 220 * k, 255 * k, 255)
    return image(size, size, fn)


def bolt(w, h, rgb):
    """Additive: a laser bolt, bright core across the width, fading at the ends."""
    def fn(u, v, x, y):
        across = max(0.0, 1 - abs(u - 0.5) * 2) ** 2
        along = min(1.0, min(v, 1 - v) * 6)
        core = across ** 4
        return (min(255, rgb[0] * across * along + 255 * core * along),
                min(255, rgb[1] * across * along + 255 * core * along),
                min(255, rgb[2] * across * along + 255 * core * along), 255)
    return image(w, h, fn)


def debris(size, seed):
    """Alpha-tested: a dark irregular chunk (alpha 0 or 255: the G100's key)."""
    def fn(u, v, x, y):
        a = math.atan2(v - 0.5, u - 0.5)
        r = math.hypot(u - 0.5, v - 0.5) * 2
        edge = 0.55 + 0.35 * value_noise(a * 1.2 + 4, 0.5, seed, 8)
        n = fbm(u, v, seed + 1, base=4, octaves=2)
        k = 40 + 60 * n
        return (k, k * 0.95, k * 0.9, 255 if r < edge else 0)
    return image(size, size, fn)
