#!/usr/bin/env python3
"""Replay every level, model and scene test on the host (build/host/screplay).

  scene_stats.py [--size WxH] [--tests ID,...] [--save FILE] [--against FILE]

Each test runs three times, its frames drawn forward, reversed and shuffled;
the per-frame hashes must agree (frame() is a pure function of the frame
number). Prints what each test asks of a card per frame: triangles
submitted (average/maximum), on screen, draw calls, texture binds, fill
(screens of pixels: average/maximum, and of them blended) and texture
memory. --save writes the hashes; --against compares with a saved file
(a refactoring that must not change any frame). Exit status 1 on any
difference. Standard library only.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import registry  # noqa: E402

EXE = os.path.join(ROOT, "build", "host", "screplay")


def replay(t, order, size):
    cmd = [EXE, t["impl"], t["file"], "--param", str(t["param"]), "--order", order, "--size", size,
           "--data", os.path.join(ROOT, "build", "data")]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if p.returncode:
        return None, (p.stderr or p.stdout).strip()
    return dict(kv.split("=", 1) for kv in p.stdout.split() if "=" in kv), None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", default="640x480")
    ap.add_argument("--tests", default="")
    ap.add_argument("--save")
    ap.add_argument("--against")
    a = ap.parse_args()
    want = set(a.tests.split(",")) - {""}
    tests = [t for t in registry.tests_in_order()
             if not t["suite"] and t["impl"] in ("level", "model", "scene") and (not want or t["id"] in want)]
    old = json.load(open(a.against)) if a.against else {}
    hashes, bad = {}, 0
    print("%-7s %9s %7s %6s %5s %10s %6s %7s  %s" % ("test", "tris", "shown", "draws", "binds", "fill",
                                                     "blend", "tex_kb", "determinism"))
    for t in tests:
        runs = {}
        for order in ("fwd", "rev", "shuf"):
            r, err = replay(t, order, a.size)
            if err:
                print("%-7s %s" % (t["id"], err))
                break
            runs[order] = r
        if len(runs) < 3:
            if "skipped" not in err:
                bad += 1
            continue
        r = runs["fwd"]
        same = len({x["hash"] for x in runs.values()}) == 1
        note = "ok" if same else "DIFFERS: " + " ".join("%s=%s" % (k, v["hash"]) for k, v in runs.items())
        want = t["budget"].get("tris")
        avg = float(r["tris"].split("/")[0])
        if want and not 0.75 * want <= avg <= 1.3 * want:
            note += "; %.0f triangles a frame, budget %d" % (avg, want)
            same = False
        if t["id"] in old and old[t["id"]] != r["hash"]:
            note += "; CHANGED from %s" % old[t["id"]]
            same = False
        bad += not same
        hashes[t["id"]] = r["hash"]
        print("%-7s %9s %7s %6s %5s %10s %6s %7s  %s" % (t["id"], r["tris"], r["shown"], r["draws"].split("/")[0],
                                                         r["binds"], r["fill"], r["blend"], r["tex_kb"], note))
    if a.save:
        json.dump(hashes, open(a.save, "w"), indent=1, sort_keys=True)
    print("scene-stats: %d tests, %d problems" % (len(hashes), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
