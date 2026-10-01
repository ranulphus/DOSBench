#!/usr/bin/env python3
"""DOSBench's test registry: src/core/tests.json, read by everything.

  registry.py gen OUT.h       the C header both programs and DBMENU include
  registry.py check           validate the registry (also against tools/games.json)
  registry.py list [SEL]      the tests a selection runs, in run order
  registry.py selftest        the selection fixture tests/fixtures/select.txt

tools/report.py and tools/run.py import it. Standard library only.
"""
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGISTRY = os.path.join(ROOT, "src", "core", "tests.json")
GAMES = os.path.join(ROOT, "tools", "games.json")
IMPLS = ("basic", "fill", "tri", "tex", "sub", "state", "model", "level", "scene")

_reg = None


def load():
    """The registry with every test's inherited and default fields filled in."""
    global _reg
    if _reg is None:
        reg = json.load(open(REGISTRY))
        by = {}
        gsecs = {g["id"]: g.get("secs", 0) for g in reg["groups"]}
        for t in reg["tests"]:
            if t.get("parent"):
                t["group"] = by[t["parent"]]["group"]
            t.setdefault("secs", gsecs.get(t["group"], 0))
            t.setdefault("parent", "")
            t.setdefault("suite", False)
            t.setdefault("metric", "fps")
            t.setdefault("unit", "fps")
            t.setdefault("weight", 0)
            t.setdefault("param", 0)
            t.setdefault("file", "")
            t.setdefault("gl_only", False)
            t.setdefault("headline", "")
            t.setdefault("derive", "")
            by[t["id"]] = t
        reg["by_id"] = by
        _reg = reg
    return _reg


def tests_in_order():
    """Every row, suites included, in run order."""
    return load()["tests"]


def entry(tid):
    return load()["by_id"].get(tid)


def primary(tid):
    """(T-line key, unit) a test is judged by; fps for anything not in the registry (the games)."""
    t = entry(tid)
    return (t["metric"], t["unit"]) if t else ("fps", "fps")


def group_title(gid):
    for g in load()["groups"]:
        if g["id"] == gid:
            return g["title"]
    return {"game": "Real games (not in the score)"}.get(gid, gid)


def order_index(tid):
    for i, t in enumerate(tests_in_order()):
        if t["id"] == tid:
            return i
    return 10000


def _tokens(sel):
    return [x for x in sel.replace(" ", ",").split(",") if x]


def selected(t, sel):
    """Whether a leaf test runs for a --tests list (bench.c and select.c follow the same rules)."""
    reg = load()
    presets = {p["id"]: p["tests"] for p in reg["presets"]}
    for tok in _tokens(sel):
        if tok in presets:
            if selected(t, presets[tok]):
                return True
        elif (tok == "all" or tok == t["group"] or tok == t["id"] or tok == t["parent"]
              or (len(tok) <= 3 and t["id"].startswith(tok))):
            return True
    return False


def select(sel):
    """The leaf tests a --tests list runs, in run order."""
    return [t["id"] for t in tests_in_order() if not t["suite"] and selected(t, sel)]


def derived_pairs():
    """{test: baseline}: state-change cost = (test avg_ms - baseline avg_ms) / changes."""
    return {t["id"]: t["derive"] for t in tests_in_order() if t["derive"]}


def scored():
    return [t["id"] for t in tests_in_order() if t["weight"] > 0]


def score(fps_by_id, weights=None):
    """The score from {test: fps}; None unless every scored test has a positive fps.
    weights {test: weight} replaces the registry's (report.py's selftest)."""
    reg = load()
    if weights is None:
        weights = {i: entry(i)["weight"] for i in scored()}
    ws = [(w, fps_by_id.get(i)) for i, w in weights.items()]
    if not ws or any(f is None or f <= 0 for _, f in ws):
        return None
    return score_of(ws, reg["score"]["scale"])


def check():
    reg = load()
    errs = []
    ids = set()
    groups = {g["id"] for g in reg["groups"]}
    presets = {p["id"] for p in reg["presets"]}
    for t in reg["tests"]:
        tid = t["id"]
        if tid in ids:
            errs.append("%s: duplicate id" % tid)
        ids.add(tid)
        if len(tid) > 7 or not tid.isalnum() or tid != tid.upper():
            errs.append("%s: ids are at most 7 upper-case letters or digits (8.3 shot names)" % tid)
        if tid.lower() in groups or tid.lower() in presets or tid.lower() == "all":
            errs.append("%s: clashes with a group or preset name" % tid)
        if t["group"] not in groups:
            errs.append("%s: unknown group %s" % (tid, t["group"]))
        if t["suite"]:
            if t.get("impl"):
                errs.append("%s: a suite has no impl" % tid)
            if t["headline"] and (t["headline"] not in reg["by_id"] or reg["by_id"][t["headline"]]["parent"] != tid):
                errs.append("%s: headline %s is not one of its phases" % (tid, t["headline"]))
            continue
        if t["parent"] and (t["parent"] not in reg["by_id"] or not reg["by_id"][t["parent"]]["suite"]):
            errs.append("%s: parent %s is not a suite listed before it" % (tid, t["parent"]))
        if t.get("impl") not in IMPLS:
            errs.append("%s: unknown impl %s" % (tid, t.get("impl")))
        if t["derive"] and t["derive"] not in reg["by_id"]:
            errs.append("%s: derive baseline %s unknown" % (tid, t["derive"]))
        if t["weight"] and (t["gl_only"] or t["group"] != "scene"):
            errs.append("%s: only scene tests that run on both APIs are scored" % tid)
    leaves = [t for t in reg["tests"] if not t["suite"]]
    if len(leaves) > 96:
        errs.append("%d tests: the menu holds at most 96" % len(leaves))
    for p in reg["presets"]:
        for tok in _tokens(p["tests"]):
            if tok not in groups and tok not in ids:
                errs.append("preset %s: unknown %s" % (p["id"], tok))
    if os.path.exists(GAMES):
        for gid in json.load(open(GAMES)):
            if gid in ids:
                errs.append("%s: in both the registry and tools/games.json" % gid)
    return errs


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', "\\\"") + '"'


def gen(out):
    reg = load()
    lines = ["/* GENERATED by tools/registry.py from src/core/tests.json - do not edit.",
             " * X-macros: define the ones you need before including (no include guard). */",
             "#ifndef DB_IMPL", "#define DB_IMPL(name)", "#endif",
             "#ifndef DB_GROUP", "#define DB_GROUP(id, title)", "#endif",
             "#ifndef DB_PRESET", "#define DB_PRESET(id, title, tests)", "#endif",
             "#ifndef DB_SUITE", "#define DB_SUITE(id, group, title, what, headline)", "#endif",
             "#ifndef DB_TEST",
             "#define DB_TEST(id, group, parent, impl, param, file, flags, metric, unit, weight, secs, derive, title, what)",
             "#endif"]
    used = []
    for t in reg["tests"]:
        if not t["suite"] and t["impl"] not in used:
            used.append(t["impl"])
    lines += ["DB_IMPL(%s)" % i for i in used]
    lines += ["DB_GROUP(%s, %s)" % (c_str(g["id"]), c_str(g["title"])) for g in reg["groups"]]
    lines += ["DB_PRESET(%s, %s, %s)" % (c_str(p["id"]), c_str(p["title"]), c_str(p["tests"])) for p in reg["presets"]]
    for t in reg["tests"]:
        if t["suite"]:
            lines.append("DB_SUITE(%s, %s, %s, %s, %s)" % (c_str(t["id"]), c_str(t["group"]), c_str(t["title"]),
                                                          c_str(t["what"]), c_str(t["headline"])))
        else:
            flags = (1 if t["gl_only"] else 0) | (2 if t["file"] else 0)
            lines.append("DB_TEST(%s, %s, %s, %s, %d, %s, %d, %s, %s, %d, %.1f, %s, %s, %s)" % (
                c_str(t["id"]), c_str(t["group"]), c_str(t["parent"]), t["impl"], t["param"],
                c_str(t["file"]) if t["file"] else "NULL", flags, c_str(t["metric"]), c_str(t["unit"]),
                t["weight"], t["secs"], c_str(t["derive"]), c_str(t["title"]), c_str(t["what"])))
    sc = reg["score"]
    lines += ["#undef DB_IMPL", "#undef DB_GROUP", "#undef DB_PRESET", "#undef DB_SUITE", "#undef DB_TEST",
              "#ifndef DB_SCORE_SCALE", "#define DB_SCORE_SCALE %.1f" % sc["scale"],
              "#define DB_SCORE_VER %d" % sc["scorever"], "#endif"]
    text = "\n".join(lines) + "\n"
    if not os.path.exists(out) or open(out).read() != text:   # keep the date: no needless rebuilds
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        open(out, "w").write(text)


def score_of(pairs, scale=None):
    """scale x the weighted geometric mean of [(weight, fps)]; 0 if any is not positive (score.c's score_of)."""
    if scale is None:
        scale = load()["score"]["scale"]
    if not pairs or any(w <= 0 or f <= 0 for w, f in pairs):
        return 0.0
    return scale * math.exp(sum(w * math.log(f) for w, f in pairs) / sum(w for w, _ in pairs))


def selftest_score():
    """tests/fixtures/score.txt: 'w:fps ... -> score' per line."""
    bad = n = 0
    for line in open(os.path.join(ROOT, "tests", "fixtures", "score.txt")):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        left, _, want = line.partition("->")
        pairs = [(float(w), float(f)) for w, f in (x.split(":") for x in left.split())]
        n += 1
        if round(score_of(pairs, 100.0)) != int(want):
            print("registry selftest: score %s = %.1f, want %s" % (left.strip(), score_of(pairs, 100.0), want.strip()))
            bad += 1
    print("registry selftest: %d score cases, %d failures" % (n, bad))
    return bad


def selftest():
    """tests/fixtures/select.txt: '<list> -> <ids in run order>' per line."""
    path = os.path.join(ROOT, "tests", "fixtures", "select.txt")
    bad = 0
    n = 0
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        sel, _, want = line.partition("->")
        got = " ".join(select(sel.strip()))
        n += 1
        if got != " ".join(want.split()):
            print("registry selftest: %s -> %s, want %s" % (sel.strip(), got, want.strip()))
            bad += 1
    print("registry selftest: %d cases, %d failures" % (n, bad))
    return bad


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "gen" and len(sys.argv) == 3:
        errs = check()
        if errs:
            sys.exit("registry: " + "\nregistry: ".join(errs))
        gen(sys.argv[2])
    elif cmd == "check":
        errs = check()
        for e in errs:
            print("registry: " + e)
        sys.exit(1 if errs else 0)
    elif cmd == "list":
        print(" ".join(select(sys.argv[2] if len(sys.argv) > 2 else "all")))
    elif cmd == "selftest":
        sys.exit(1 if selftest() + selftest_score() else 0)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
