#!/usr/bin/env python3
"""Fetch and convert DOSBench's scenes (docs/content.md).

  assets.py fetch            download the pinned sources into ~/.cache/dosbench/dl
  assets.py convert [NAMES]  write build/data/<NAME>.DBS and build/data/CREDITS.TXT
  assets.py list             the scenes, their sources and licences

Sources are pinned by URL and sha256 and never committed; converted files
are build outputs. The Stanford scans are for research use: they are
fetched from Stanford by each user and never redistributed.

Neither API path lights vertices (DOS-GL has no lighting), so lighting is
baked into vertex colours here: a fixed directional light in world space,
with the camera orbiting the model.
"""
import argparse
import glob
import hashlib
import io
import json
import math
import os
import re
import struct
import sys
import tarfile
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import dbs  # noqa: E402

CACHE = os.environ.get("DOSBENCH_CACHE", os.path.expanduser("~/.cache/dosbench"))
DL = os.path.join(CACHE, "dl")
OUT = os.path.join(ROOT, "build", "data")

KHRONOS = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/7d4ba189827916452eeadc82d4b712dbc6280a6f/Models"
SOURCES = {
    "teapot_bezier": ("https://users.cs.utah.edu/~dejohnso/models/teapot_bezier",
                      "22d59967e342b8e6f2b4e78e8db50a44f5aa370661f069b478ae8d0d76d9395e"),
    "bunny.tar.gz": ("http://graphics.stanford.edu/pub/3Dscanrep/bunny.tar.gz",
                     "a5720bd96d158df403d153381b8411a727a1d73cff2f33dc9b212d6f75455b84"),
    "dragon_recon.tar.gz": ("http://graphics.stanford.edu/pub/3Dscanrep/dragon/dragon_recon.tar.gz",
                            "74ac1d90989c9b1732edee82d57e9ce71452144cf4355f108d8c9c616d28d02f"),
    "Lantern.glb": (KHRONOS + "/Lantern/glTF-Binary/Lantern.glb",
                    "a79458c4b02d695187a952f23a63b8bf278e7bc3d316a3c2a314f2d6974181f1"),
    "Avocado.glb": (KHRONOS + "/Avocado/glTF-Binary/Avocado.glb",
                    "ccc9c3ce56423720b09399c2351537207cd5a65f859f9e6e2f30922762f3abd4"),
    "Suzanne.gltf": (KHRONOS + "/Suzanne/glTF/Suzanne.gltf",
                     "7e8ae013010aff530162ef2795cec74c2646019e224af17bcfb691664f0f0aec"),
    "Suzanne.bin": (KHRONOS + "/Suzanne/glTF/Suzanne.bin",
                    "b85c2727aa41318e00673d8892f5879d46fb6e476e280f28ee1febd07602b6b8"),
    "Suzanne_BaseColor.png": (KHRONOS + "/Suzanne/glTF/Suzanne_BaseColor.png",
                              "1c009f45e53b260ceecd383f2c356ed08b08e05acd4633bcc713213319b2092b"),
    "lite.zip": ("https://github.com/lavenderdotpet/LibreQuake/releases/download/v0.09-beta/lite.zip",
                 "428e736b2f01d953e09a08c60bee975bdc4a0ac2219e97fa095c8af41754da83"),
}

UTAH = ("Utah teapot (Martin Newell, 1975; Bezier data as published by the University of Utah "
        "Model Repository)", "freely available; credit the University of Utah")
STANFORD = ("Stanford 3D Scanning Repository, Stanford University Computer Graphics Laboratory",
            "research use; not redistributed (fetched from Stanford by each user)")
CC0 = "CC0-1.0"

# name: (converter, arguments, sources, credit, licence, description)
SCENES = {
    "TPOT4K": ("teapot", {"tris": 4096}, ["teapot_bezier"], UTAH[0], UTAH[1], "Utah teapot, 4k triangles"),
    "TPOT16K": ("teapot", {"tris": 16384}, ["teapot_bezier"], UTAH[0], UTAH[1], "Utah teapot, 16k triangles"),
    "TPOT64K": ("teapot", {"tris": 65536}, ["teapot_bezier"], UTAH[0], UTAH[1], "Utah teapot, 64k triangles"),
    "BUNNY": ("ply", {"tar": "bunny.tar.gz", "member": "bunny/reconstruction/bun_zipper.ply",
                      "rgb": (205, 180, 150)}, ["bunny.tar.gz"], "Stanford Bunny; " + STANFORD[0], STANFORD[1],
              "Stanford bunny, 69k triangles"),
    "DRAGON": ("ply", {"tar": "dragon_recon.tar.gz", "member": "dragon_recon/dragon_vrip_res3.ply",
                       "rgb": (120, 180, 120)}, ["dragon_recon.tar.gz"], "Stanford Dragon; " + STANFORD[0],
               STANFORD[1], "Stanford dragon (res3), 48k triangles"),
    "LANTERN": ("gltf", {"file": "Lantern.glb"}, ["Lantern.glb"],
                "Lantern by sbtron (Microsoft), Khronos glTF Sample Assets", CC0, "Lantern, textured"),
    "SUZANNE": ("gltf", {"file": "Suzanne.gltf"}, ["Suzanne.gltf", "Suzanne.bin", "Suzanne_BaseColor.png"],
                "Suzanne by Norbert Nopper (UX3D), Khronos glTF Sample Assets", CC0, "Suzanne, textured"),
    "AVOCADO": ("gltf", {"file": "Avocado.glb"}, ["Avocado.glb"],
                "Avocado (Microsoft), Khronos glTF Sample Assets", CC0, "Avocado, textured"),
    # Game scenes: made by a generator function ("module:function", in tools/), nothing fetched.
    "GSTEST": ("game", "dbs:selftest2_scene", [], "DOSBench (procedural)", CC0,
               "the scene runtime's check: every feature in one small scene"),
}
LIGHT = (0.40, 0.80, 0.45)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(names=None):
    os.makedirs(DL, exist_ok=True)
    ok = True
    for name, (url, want) in SOURCES.items():
        if names and name not in names:
            continue
        path = os.path.join(DL, name)
        if os.path.exists(path) and sha256(path) == want:
            continue
        print("fetch: %s" % url)
        tmp = path + ".part"
        with urllib.request.urlopen(url, timeout=120) as r, open(tmp, "wb") as f:
            while True:
                b = r.read(1 << 20)
                if not b:
                    break
                f.write(b)
        got = sha256(tmp)
        if got != want:
            print("fetch: %s sha256 %s, expected %s" % (name, got, want))
            os.remove(tmp)
            ok = False
            continue
        os.replace(tmp, path)
    return ok


def src(name):
    path = os.path.join(DL, name)
    if not os.path.exists(path):
        raise SystemExit("assets: %s missing: run assets.py fetch" % name)
    return path


# ---- geometry helpers --------------------------------------------------------
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(v):
    l = math.sqrt(dot(v, v))
    return (v[0] / l, v[1] / l, v[2] / l) if l > 1e-12 else (0.0, 1.0, 0.0)


LIGHT_N = norm(LIGHT)


def vertex_normals(pos, tris):
    acc = [[0.0, 0.0, 0.0] for _ in pos]
    for a, b, c in tris:
        n = cross(sub(pos[b], pos[a]), sub(pos[c], pos[a]))      # area-weighted
        for i in (a, b, c):
            acc[i][0] += n[0]; acc[i][1] += n[1]; acc[i][2] += n[2]
    return [norm(n) for n in acc]


def signed_volume(pos, tris):
    return sum(dot(pos[a], cross(pos[b], pos[c])) for a, b, c in tris) / 6.0


def shade(n, rgb, two_sided=False):
    d = dot(n, LIGHT_N)
    k = 0.30 + 0.70 * (abs(d) if two_sided else max(0.0, d))
    return tuple(min(255, int(c * k + 0.5)) for c in rgb)


def normalise(pos, radius=10.0):
    """Centre on the bounding box and scale to the given bounding-sphere radius."""
    lo = [min(p[k] for p in pos) for k in range(3)]
    hi = [max(p[k] for p in pos) for k in range(3)]
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    r = max(math.sqrt(dot(sub(p, c), sub(p, c))) for p in pos) or 1.0
    s = radius / r
    return [((p[0] - c[0]) * s, (p[1] - c[1]) * s, (p[2] - c[2]) * s) for p in pos]


def orbit_view(radius=10.0):
    return dict(kind=0, frames=360, znear=1.0, zfar=200.0, centre=(0.0, 0.0, 0.0), radius=radius * 2.1,
                height=radius * 0.6)


def add_mesh(scene, pos, tris, colours, uvs=None, tex=dbs.NONE, flags=0):
    """Add a mesh, split into batches of at most 65536 vertices."""
    start = 0
    while start < len(tris):
        remap, verts, out = {}, [], []
        i = start
        while i < len(tris) and len(remap) + 3 <= 65536:
            t = []
            for k in tris[i]:
                if k not in remap:
                    remap[k] = len(verts)
                    u, v = uvs[k] if uvs else (0.0, 0.0)
                    verts.append((pos[k][0], pos[k][1], pos[k][2]) + tuple(colours[k]) + (255, u, v, u, v))
                t.append(remap[k])
            out.append(t)
            i += 1
        scene.add_batch(verts, out, tex=tex, flags=flags)
        start = i


# ---- converters --------------------------------------------------------------
def conv_teapot(args):
    text = open(src("teapot_bezier")).read()
    pts = [tuple(float(x) for x in m) for m in re.findall(r"pt\(\s*([-\d.eE]+),\s*([-\d.eE]+),\s*([-\d.eE]+)\s*\)", text)]
    patches = [pts[i:i + 16] for i in range(0, len(pts) - 15, 16)]
    n = max(1, int(round(math.sqrt(args["tris"] / (2.0 * len(patches))))))

    def bern(t):
        s = 1 - t
        return (s * s * s, 3 * t * s * s, 3 * t * t * s, t * t * t)
    pos, tris = [], []
    for p in patches:
        base = len(pos)
        for i in range(n + 1):
            bu = bern(i / n)
            for j in range(n + 1):
                bv = bern(j / n)
                x = y = z = 0.0
                for a in range(4):
                    for b in range(4):
                        w = bu[a] * bv[b]
                        q = p[a * 4 + b]
                        x += w * q[0]; y += w * q[1]; z += w * q[2]
                pos.append((x, y, z))
        for i in range(n):
            for j in range(n):
                a = base + i * (n + 1) + j
                tris.append((a, a + n + 1, a + 1))
                tris.append((a + 1, a + n + 1, a + n + 2))
    pos = normalise(pos)
    normals = vertex_normals(pos, tris)
    colours = [shade(nm, (235, 225, 205), two_sided=True) for nm in normals]
    s = dbs.Scene()
    add_mesh(s, pos, tris, colours, flags=dbs.BF_TWOSIDED)
    s.view = orbit_view()
    s.info["patches"] = str(len(patches))
    s.info["tessellation"] = str(n)
    return s


def read_ply(data):
    """ASCII or binary little-endian PLY: vertices (x, y, z) and triangular faces."""
    head_end = data.index(b"\n", data.index(b"end_header")) + 1
    header = data[:head_end].decode("latin-1").splitlines()
    fmt, elements, cur = "ascii", [], None
    for line in header:
        w = line.split()
        if not w:
            continue
        if w[0] == "format":
            fmt = w[1]
        elif w[0] == "element":
            cur = [w[1], int(w[2]), []]
            elements.append(cur)
        elif w[0] == "property":
            cur[2].append(w[1:])
    pos, tris = [], []
    if fmt == "ascii":
        lines = data[head_end:].decode("latin-1").split("\n")
        li = 0
        for name, count, props in elements:
            for _ in range(count):
                vals = lines[li].split()
                li += 1
                if name == "vertex":
                    names = [p[-1] for p in props]
                    pos.append(tuple(float(vals[names.index(k)]) for k in ("x", "y", "z")))
                elif name == "face":
                    idx = [int(v) for v in vals[1:1 + int(vals[0])]]
                    for k in range(1, len(idx) - 1):
                        tris.append((idx[0], idx[k], idx[k + 1]))
        return pos, tris
    if fmt != "binary_little_endian":
        raise ValueError("PLY format %s" % fmt)
    code = {"float": "f", "float32": "f", "double": "d", "uchar": "B", "uint8": "B", "char": "b", "int": "i",
            "int32": "i", "uint": "I", "uint32": "I", "short": "h", "ushort": "H"}
    off = head_end
    for name, count, props in elements:
        if name == "face":
            lt, it = code[props[0][1]], code[props[0][2]]
            for _ in range(count):
                n = struct.unpack_from("<" + lt, data, off)[0]
                off += struct.calcsize(lt)
                idx = struct.unpack_from("<%d%s" % (n, it), data, off)
                off += n * struct.calcsize(it)
                for k in range(1, n - 1):
                    tris.append((idx[0], idx[k], idx[k + 1]))
        else:
            f = "<" + "".join(code[p[0]] for p in props)
            names = [p[-1] for p in props]
            size = struct.calcsize(f)
            for _ in range(count):
                vals = struct.unpack_from(f, data, off)
                off += size
                if name == "vertex":
                    pos.append(tuple(vals[names.index(k)] for k in ("x", "y", "z")))
    return pos, tris


def conv_ply(args):
    with tarfile.open(src(args["tar"])) as t:
        data = t.extractfile(args["member"]).read()
    pos, tris = read_ply(data)
    # Drop degenerate faces and orient outwards (positive volume).
    tris = [t for t in tris if len(set(t)) == 3]
    pos = normalise(pos)
    if signed_volume(pos, tris) < 0:
        tris = [(a, c, b) for a, b, c in tris]
    normals = vertex_normals(pos, tris)
    colours = [shade(nm, args["rgb"]) for nm in normals]
    s = dbs.Scene()
    add_mesh(s, pos, tris, colours)
    s.view = orbit_view()
    return s


# glTF
COMP = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load_gltf(path):
    data = open(path, "rb").read()
    if data[:4] == b"glTF":
        ln = struct.unpack_from("<I", data, 12)[0]
        j = json.loads(data[20:20 + ln])
        off = 20 + ln
        bln = struct.unpack_from("<I", data, off)[0]
        buffers = [data[off + 8:off + 8 + bln]]
    else:
        j = json.loads(data)
        buffers = [open(os.path.join(os.path.dirname(path), b["uri"]), "rb").read() for b in j["buffers"]]
    return j, buffers


def accessor(j, buffers, i):
    a = j["accessors"][i]
    bv = j["bufferViews"][a["bufferView"]]
    buf = buffers[bv.get("buffer", 0)]
    c, n = COMP[a["componentType"]], NCOMP[a["type"]]
    size = struct.calcsize("<" + c)
    stride = bv.get("byteStride") or size * n
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    out = []
    for k in range(a["count"]):
        v = struct.unpack_from("<%d%s" % (n, c), buf, base + k * stride)
        out.append(v if n > 1 else v[0])
    return out


def image_bytes(j, buffers, path, img):
    im = j["images"][img]
    if "uri" in im:
        return open(os.path.join(os.path.dirname(path), im["uri"]), "rb").read()
    bv = j["bufferViews"][im["bufferView"]]
    o = bv.get("byteOffset", 0)
    return buffers[bv.get("buffer", 0)][o:o + bv["byteLength"]]


def mat_mul(a, b):
    return [sum(a[r + 4 * k] * b[k + 4 * c] for k in range(4)) for c in range(4) for r in range(4)]


def node_matrix(n):
    if "matrix" in n:
        return list(n["matrix"])
    tx, ty, tz = n.get("translation", (0, 0, 0))
    x, y, z, w = n.get("rotation", (0, 0, 0, 1))
    sx, sy, sz = n.get("scale", (1, 1, 1))
    r = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
         0, 0, 0, 1]
    for k in range(3):
        r[k] *= sx; r[4 + k] *= sy; r[8 + k] *= sz
    r[12], r[13], r[14] = tx, ty, tz
    return r


def xform(m, p, w=1.0):
    return tuple(m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * w for r in range(3))


def conv_gltf(args):
    path = src(args["file"])
    j, buffers = load_gltf(path)
    ident = node_matrix({})
    prims = []                                  # (matrix, primitive)

    def walk(ni, parent):
        n = j["nodes"][ni]
        m = mat_mul(parent, node_matrix(n))
        if "mesh" in n:
            for p in j["meshes"][n["mesh"]]["primitives"]:
                prims.append((m, p))
        for c in n.get("children", []):
            walk(c, m)
    roots = j["scenes"][j.get("scene", 0)]["nodes"] if "scenes" in j else range(len(j["nodes"]))
    for r in roots:
        walk(r, ident)
    # Gather world-space geometry per material.
    groups = {}
    for m, p in prims:
        if p.get("mode", 4) != 4:
            continue
        pos = [xform(m, v) for v in accessor(j, buffers, p["attributes"]["POSITION"])]
        nrm = ([norm(xform(m, v, 0.0)) for v in accessor(j, buffers, p["attributes"]["NORMAL"])]
               if "NORMAL" in p["attributes"] else None)
        uv = accessor(j, buffers, p["attributes"]["TEXCOORD_0"]) if "TEXCOORD_0" in p["attributes"] else None
        idx = accessor(j, buffers, p["indices"]) if "indices" in p else list(range(len(pos)))
        tris = [tuple(idx[k:k + 3]) for k in range(0, len(idx) - 2, 3)]
        if nrm is None:
            nrm = vertex_normals(pos, tris)
        g = groups.setdefault(p.get("material", -1), {"pos": [], "nrm": [], "uv": [], "tris": []})
        base = len(g["pos"])
        g["pos"] += pos
        g["nrm"] += nrm
        g["uv"] += uv or [(0.0, 0.0)] * len(pos)
        g["tris"] += [(a + base, b + base, c + base) for a, b, c in tris]
    # One normalisation for the whole model.
    allpos = [p for g in groups.values() for p in g["pos"]]
    lo = [min(p[k] for p in allpos) for k in range(3)]
    hi = [max(p[k] for p in allpos) for k in range(3)]
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    r = max(math.sqrt(dot(sub(p, c), sub(p, c))) for p in allpos) or 1.0
    s = dbs.Scene()
    texmap = {}
    for mat, g in sorted(groups.items()):
        tex, factor = dbs.NONE, (1.0, 1.0, 1.0, 1.0)
        if mat >= 0:
            pbr = j["materials"][mat].get("pbrMetallicRoughness", {})
            factor = pbr.get("baseColorFactor", factor)
            if "baseColorTexture" in pbr:
                img = j["textures"][pbr["baseColorTexture"]["index"]]["source"]
                if img not in texmap:
                    w, h, rgba = dbs.png_decode(image_bytes(j, buffers, path, img))
                    w, h, rgba = dbs.fit_texture(w, h, rgba, 256)
                    texmap[img] = s.add_texture(w, h, rgba)
                tex = texmap[img]
        pos = [((p[0] - c[0]) * 10 / r, (p[1] - c[1]) * 10 / r, (p[2] - c[2]) * 10 / r) for p in g["pos"]]
        base = (255 * factor[0], 255 * factor[1], 255 * factor[2])
        colours = [shade(nm, base) for nm in g["nrm"]]
        add_mesh(s, pos, g["tris"], colours, uvs=g["uv"], tex=tex)
    s.view = orbit_view()
    return s


def conv_game(args):
    """A game scene from its generator, "module:function" in tools/; it must pass Scene.check()."""
    import importlib
    mod, fn = args.split(":")
    s = getattr(importlib.import_module(mod), fn)()
    probs = s.check()
    if probs:
        raise SystemExit("convert: %s: %s" % (args, "; ".join(probs)))
    return s


CONVERTERS = {"teapot": conv_teapot, "ply": conv_ply, "gltf": conv_gltf, "game": conv_game}


def convert(names=None, force=False):
    os.makedirs(OUT, exist_ok=True)
    tools = [os.path.join(HERE, f) for f in ("assets.py", "dbs.py", "bsp.py") if os.path.exists(os.path.join(HERE, f))]
    tools += glob.glob(os.path.join(HERE, "scene_*.py")) + glob.glob(os.path.join(HERE, "gsgen.py"))
    stamp = max(os.path.getmtime(t) for t in tools)
    credits = ["DOSBench scene data: sources and licences (docs/content.md).", ""]
    scenes = dict(SCENES)
    try:
        import bsp
        scenes.update(bsp.SCENES)
        CONVERTERS["bsp"] = bsp.convert
    except ImportError:
        pass
    for name, (kind, args, sources, credit, licence, what) in scenes.items():
        credits.append("%-8s %s\n         %s. Licence: %s." % (name + ".DBS", what, credit, licence))
        if names and name not in names:
            continue
        out = os.path.join(OUT, name + ".DBS")
        newest = max([stamp] + [os.path.getmtime(src(f)) for f in sources])
        if not force and os.path.exists(out) and os.path.getmtime(out) >= newest:
            continue
        s = CONVERTERS[kind](args)
        s.info.update(name=name, what=what, credit=credit, licence=licence)
        size = s.write(out)
        print("convert: %-8s %6d tris %5d verts %3d batches %2d textures %7d B tex16 %8d B" %
              (name, s.triangles(), len(s.verts), len(s.batches), len(s.textures), s.texture_bytes16(), size))
    open(os.path.join(OUT, "CREDITS.TXT"), "w", newline="\r\n").write("\n".join(credits) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("steps", nargs="+", help="fetch, convert, list, or scene names for convert")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    names = [s for s in a.steps if s not in ("fetch", "convert", "list")]
    if "list" in a.steps:
        for name, (kind, _, sources, credit, licence, what) in SCENES.items():
            print("%-8s %-6s %s | %s | %s" % (name, kind, what, credit, licence))
    if "fetch" in a.steps and not fetch():
        return 1
    if "convert" in a.steps:
        convert(names or None, a.force)
    return 0


if __name__ == "__main__":
    sys.exit(main())
