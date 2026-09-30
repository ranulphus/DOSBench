#!/usr/bin/env python3
"""DOSBench runner.

  run.py loopa [--card g450[,g400,g200]] [--tests LIST] [--no-ref] [--full]
      One 86Box VM per card (MGA-Glide's Loop A harness, the emulated Matrox
      card plus the emulated Voodoo Graphics) runs, in one job:
        BENCHGL.EXE                                 OpenGL on DOS-GL      (tag L)
        BENCHG.EXE --glide=C:\\TEST\\GLIDE2X.OVL      Glide on MGA-Glide    (tag G)
        BENCHG.EXE --glide=C:\\REF\\GLIDE2X.OVL       Glide on 3dfx's OVL on
                                                    the emulated Voodoo   (tag V)
      then checks the frames (docs/testing.md): Glide on MGA-Glide against the
      Voodoo, OpenGL across cards, Glide against OpenGL (advisory). 86Box timing
      says nothing about real hardware: numbers come from the bench.

  run.py bench --pc NAME [--tests LIST] [--modes ...] [--install-data]
      A bench job (MGA-Glide's tools/bench/run.py): both programs back to back
      on a real PC; results land in MGA-Glide's out/bench/<pc>/<job>/files/.
      --install-data first copies the scenes to C:\\DOSBENCH\\DATA (once per PC).

  run.py winvm [--card g450] [--games]
      A ready-to-boot 86Box machine (dist/dosbench-<card>-vm.zip) with DOSBench,
      DOS-GL's demos and ClassiCube, for MGA-Glide's patched 86Box, including
      its Windows build (MGA-Glide tools/86box/windows/). --games adds the
      owner's games from the local fixtures (a private zip): GTA and Screamer
      Rally on C:, and on a second disk D: Quake, LibreQuake and Quake 2 in
      DOS-GL's builds (DOS-GL: make quake).

  run.py games [--card g450[,g400,g200]] [--tests LIST]
      The game tests (tools/games.json, group game): timedemos of DOS-GL's
      builds of the Quake ports (DOS-GL: make quake), one Loop A job per card
      and game on the owner's game data (local fixtures), each writing its own
      H/T lines and frame; then checks each frame across cards and the 8-bit
      and multitexture variants against their plain runs. Tests that need a
      GL feature DOS-GL does not have yet are skipped. Outputs go to
      out/games/<card>/<game>/.

  run.py compare DIR [DIR...]
      Re-run the image checks on existing Loop A output directories.

Outputs go to out/loopa/<card>/ (serial.log, files/RESULTS.TXT, *.png,
checks.json, status).
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def make_vars():
    """MGA_GLIDE and DOSGL as the Makefile sees them (config.mk, config.local.mk, environment)."""
    vals = {"HOME": os.path.expanduser("~")}
    for name in ("config.mk", "config.local.mk"):
        path = os.path.join(ROOT, name)
        if not os.path.exists(path):
            continue
        for line in open(path):
            m = re.match(r"\s*([A-Z_]+)\s*\??[:]?=\s*(.*?)\s*$", line)
            if m and not line.lstrip().startswith("#"):
                val = re.sub(r"\$\((\w+)\)", lambda g: vals.get(g.group(1), ""), m.group(2))
                if "?=" in line and m.group(1) in vals and m.group(1) != "HOME":
                    continue
                vals[m.group(1)] = val
    for k in ("MGA_GLIDE", "DOSGL", "FIFTHWHEEL"):
        if os.environ.get(k):
            vals[k] = os.environ[k]
    return vals


VARS = make_vars()
MGA = VARS["MGA_GLIDE"]
sys.path.insert(0, os.path.join(MGA, "tools"))
CACHE = os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide"))
REF_OVL = os.environ.get("REF_OVL", os.path.join(CACHE, "fixtures", "ovl", "gta.ovl"))
DATA_DOS = "C:\\DOSBENCH\\DATA"
CHECKS = os.path.join(ROOT, "tools", "checks.json")


def parse_serial(text):
    """Programs in order: {prog, tag, done, tests: {id: (result, detail)}, lines: [bench lines]}."""
    progs, cur = [], None
    for line in text.replace("\r", "").split("\n"):
        if line.startswith("HX-START "):
            cur = {"prog": line.split()[1], "tag": None, "done": None, "tests": {}, "bench": []}
            progs.append(cur)
        elif cur is None:
            continue
        elif line.startswith("HX-TEST "):
            parts = line.split(" ", 3)
            if len(parts) >= 3:
                cur["tests"][parts[1]] = (parts[2], parts[3] if len(parts) > 3 else "")
        elif line.startswith("HX-STAT bench "):
            rec = parse_record(line[len("HX-STAT bench "):])
            cur["bench"].append(rec)
            cur["tag"] = rec.get("tag", cur["tag"])
        elif line.startswith("HX-DONE "):
            try:
                cur["done"] = int(line.split()[1])
            except (IndexError, ValueError):
                cur["done"] = -1
    return progs


def parse_record(line):
    parts = line.split()
    rec = {"kind": parts[0]} if parts else {}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            rec[k] = v
    return rec


def data_files():
    return sorted(glob.glob(os.path.join(ROOT, "build", "data", "*.DBS")))


def loopa_job(card, a):
    out = os.path.join(ROOT, "out", "loopa", card)
    common = "--tests %s --data %s --shots --noexit" % (a.tests, DATA_DOS)
    if not a.full:
        common += " --quick"
    if a.modes:
        common += " --modes " + a.modes
    # The options go in a file (--args): DOS/4GW passes BENCHG only about 100
    # characters, which a --modes list and the Voodoo run's options exceed.
    os.makedirs(os.path.join(ROOT, "out", "loopa"), exist_ok=True)
    argfile = os.path.join(ROOT, "out", "loopa", "args-%s.txt" % card)
    open(argfile, "w", newline="\r\n").write(common + "\n")
    files = ["%s=/TEST/BENCHG.EXE" % os.path.join(ROOT, "build/dos/BENCHG.EXE"),
             "%s=/TEST/BENCHGL.EXE" % os.path.join(ROOT, "build/dos/BENCHGL.EXE"),
             "%s=/TEST/ARGS.TXT" % argfile]
    for f in data_files():
        files.append("%s=/DOSBENCH/DATA/%s" % (f, os.path.basename(f).upper()))
    common = "--args C:\\TEST\\ARGS.TXT"
    cmds = ["C:\\TEST\\BENCHGL.EXE " + common,
            "C:\\TEST\\BENCHG.EXE %s --glide=C:\\TEST\\GLIDE2X.OVL" % common]
    ref = a.ref and card == a.ref_card
    if ref:
        if not os.path.exists(REF_OVL):
            print("run.py: no reference OVL at %s (REF_OVL); skipping the Voodoo run" % REF_OVL)
            ref = False
        else:
            files.append("%s=/REF/GLIDE2X.OVL" % REF_OVL)
            cmds.append("C:\\TEST\\BENCHG.EXE %s --glide=C:\\REF\\GLIDE2X.OVL --tag V" % common)
    cmd = [os.path.join(MGA, "tools", "dev"), "python3", os.path.join(MGA, "tools", "loopa", "run.py"),
           "--name", "dosbench-" + card, "--card", card, "--out", out,
           "--ovl", os.path.join(MGA, "build", "ow", "GLIDE2X.OVL"),
           "--timeout", str(a.timeout), "--idle", str(a.idle)]
    for f in files:
        cmd += ["--file", f]
    for c in cmds:
        cmd += ["--cmd", c]
    print("loopa %s: %d programs, %d data files" % (card, len(cmds), len(data_files())))
    subprocess.run(cmd, cwd=ROOT)
    return out


def summarise(out, expect):
    """Per-program status from the serial log; returns (ok, summary)."""
    serial = os.path.join(out, "serial.log")
    text = open(serial, "rb").read().decode("latin-1") if os.path.exists(serial) else ""
    progs = parse_serial(text)
    summary, ok = [], True
    tags = [p["tag"] for p in progs]
    for tag in expect:
        if tag not in tags:
            summary.append({"tag": tag, "status": "MISSING"})
            ok = False
    for p in progs:
        fails = sorted(k for k, v in p["tests"].items() if v[0] == "FAIL")
        skips = sorted(k for k, v in p["tests"].items() if v[0] == "SKIP")
        passes = sorted(k for k, v in p["tests"].items() if v[0] == "PASS")
        st = "PASS" if p["done"] == 0 and not fails else "FAIL" if p["done"] is not None else "CRASH"
        ok = ok and st == "PASS"
        summary.append({"prog": p["prog"], "tag": p["tag"], "status": st, "done": p["done"],
                        "pass": len(passes), "fail": fails, "skip": skips})
    return ok, summary


def checks_config():
    return json.load(open(CHECKS)) if os.path.exists(CHECKS) else {}


def image_check(results, ref, got, kind, gate, test):
    """Compare two frames with the checks.json settings for kind and test."""
    import imgcmp
    cfg = checks_config()
    opts = dict(cfg.get("default", {"tol": 24, "frac": 0.005}))
    opts.update(cfg.get(kind, {}).get("default", {}))
    opts.update(cfg.get(kind, {}).get(test, {}))
    # then the frame's card (its output directory): a card's own limits
    opts.update(cfg.get("cards", {}).get(os.path.basename(os.path.dirname(got)), {}).get(test, {}))
    if opts.get("skip"):
        return
    ddir = os.path.join(os.path.dirname(got), "diff")
    os.makedirs(ddir, exist_ok=True)
    diff = os.path.join(ddir, "%s-vs-%s-%s" % (os.path.basename(got)[:-4], os.path.basename(os.path.dirname(ref)),
                                                os.path.basename(ref)))
    r = imgcmp.compare(ref, got, tol=opts["tol"], frac=opts["frac"], edge=opts.get("edge", True),
                       box=opts.get("box", False), diff_path=diff)
    r.update(kind=kind, test=test, ref=os.path.relpath(ref, ROOT), got=os.path.relpath(got, ROOT),
             gate=gate and not opts.get("advisory"))
    results.append(r)


def shots(d, tag):
    """Saved frames <tag><TEST>.png in an output directory, by test ID."""
    return {os.path.basename(p)[1:-4].upper(): p for p in glob.glob(os.path.join(d, tag.lower() + "*.png"))}


def compare_images(dirs):
    """Frame checks within and across Loop A output directories."""
    results = []

    def cmp(ref, got, kind, gate, test):
        image_check(results, ref, got, kind, gate, test)

    for d in dirs:
        g, v, l = shots(d, "g"), shots(d, "v"), shots(d, "l")
        for t in sorted(g):
            if t in v:
                cmp(v[t], g[t], "glide-vs-voodoo", True, t)
            if t in l:
                cmp(g[t], l[t], "gl-vs-glide", False, t)
    # Each API across cards: every card against the first.
    for tag, kind in (("l", "gl-across-cards"), ("g", "glide-across-cards")):
        base = shots(dirs[0], tag)
        for d in dirs[1:]:
            other = shots(d, tag)
            for t in sorted(base):
                if t in other:
                    cmp(base[t], other[t], kind, True, t)
    return results


def fill_glide_display(out):
    """Glide cannot tell BENCHG how MGA-Glide shows a size: take it from the
    runtime's MGL-WINOPEN lines (display=WxH fit=F), in order, into the H
    lines of api=glide that say display=?."""
    res, serial = os.path.join(out, "files", "RESULTS.TXT"), os.path.join(out, "serial.log")
    if not (os.path.exists(res) and os.path.exists(serial)):
        return
    opens = re.findall(r"MGL-WINOPEN (\d+x\d+) .*?display=(\d+x\d+) fit=(\w+)",
                       open(serial, "rb").read().decode("latin-1"))
    lines, changed = open(res, "rb").read().decode("latin-1").split("\n"), False
    for i, l in enumerate(lines):
        if l.startswith("H ") and " api=glide " in l and " display=? " in l + " ":
            m = re.search(r" mode=(\d+x\d+)", l)
            for j, (mode, disp, fit) in enumerate(opens):
                if m and mode == m.group(1):
                    lines[i] = l.replace(" display=?", " display=" + disp).replace(" fit=?", " fit=" + fit)
                    del opens[j]
                    changed = True
                    break
    if changed:
        open(res, "wb").write("\n".join(lines).encode("latin-1"))


def cmd_loopa(a):
    cards = a.card.split(",")
    a.ref_card = cards[0]
    dirs, all_ok = [], True
    for card in cards:
        out = loopa_job(card, a)
        fill_glide_display(out)
        dirs.append(out)
        expect = ["L", "G"] + (["V"] if a.ref and card == a.ref_card and os.path.exists(REF_OVL) else [])
        ok, summary = summarise(out, expect)
        all_ok = all_ok and ok
        for s in summary:
            if "prog" in s:
                print("  %-8s %s %-5s pass=%d fail=%s skip=%d" % (s["prog"], s["tag"], s["status"], s["pass"],
                                                                 ",".join(s["fail"]) or "-", len(s["skip"])))
            else:
                print("  %s MISSING" % s["tag"])
        json.dump(summary, open(os.path.join(out, "programs.json"), "w"), indent=1)
    all_ok = report_checks(dirs) and all_ok
    print("loopa: %s" % ("PASS" if all_ok else "FAIL"))
    return 0 if all_ok else 1


def report_checks(dirs, results=None):
    if results is None:
        results = compare_images(dirs)
    gated_bad = [r for r in results if r["gate"] and not r["ok"]]
    for r in results:
        flag = "ok " if r["ok"] else ("BAD" if r["gate"] else "adv")
        print("  %s %-16s %-8s frac=%-8s worst=%-3s %s" % (flag, r["kind"], r["test"], r.get("frac"), r.get("worst"),
                                                        r["got"]))
    json.dump(results, open(os.path.join(dirs[0], "checks.json"), "w"), indent=1)
    print("image checks: %d compared, %d gating failures" % (len(results), len(gated_bad)))
    return not gated_bad


GAMES = os.path.join(ROOT, "tools", "games.json")


def game_catalogue(sel):
    cat = {k: v for k, v in json.load(open(GAMES)).items() if not k.startswith("_")}
    if sel in ("all", "game"):
        return cat
    want = [t.strip().upper() for t in sel.split(",") if t.strip()]
    bad = [t for t in want if t not in cat]
    if bad:
        raise SystemExit("run.py: no game test %s (tools/games.json)" % ",".join(bad))
    return {k: cat[k] for k in want}


def dosgl_features(card):
    """GL features DOS-GL advertises on a card that game tests may need
    (games.json "needs"): multitexture only on the dual-texture G400 and G450."""
    have = set()
    src = os.path.join(VARS["DOSGL"], "src", "gl", "get.c")
    if os.path.exists(src) and "GL_ARB_multitexture" in open(src).read() and card in ("g400", "g450"):
        have.add("mtex")
    return have


def games_job(card, key, tests, a):
    """One Loop A job: every selected test of one game in turn, from its D: disk.
    Each game command is bracketed by HX-START/HX-DONE so the job runs on
    through the loading between them."""
    dosgl = VARS["DOSGL"]
    # The dev container mounts this repository and the fixture cache, not
    # DOS-GL: stage DOS-GL's game builds and games list here.
    hl = key == "halflife"
    tool = "halflife" if hl else "quake"
    q = rsp_dir = os.path.join(ROOT, "build", "games", tool, card)   # per card: jobs may run side by side
    os.makedirs(q, exist_ok=True)
    for f in (("HLDGL.EXE", "EXTRAS.PK3", "DOSLFN.COM") if hl else
              ("QDOSDGL.EXE", "Q2DGL.EXE", "GAMEX86.DXE", "DOSLFN.COM")):
        src = os.path.join(dosgl, "build", tool, f)
        if not os.path.exists(src):
            raise SystemExit("run.py: no %s (DOS-GL: make %s)" % (src, tool))
        shutil.copyfile(src, os.path.join(q, f))
    gfile = os.path.join(q, "games.json")
    shutil.copyfile(os.path.join(dosgl, "tools", tool, "games.json"), gfile)
    game = json.load(open(gfile))[key]
    out = os.path.join(ROOT, "out", "games", card, key)
    files, cmds, extra = [], ["D:", "CD \\" + game["cwd"]], []
    for exe in sorted(set(t["exe"] for t in tests.values())):
        files.append("%s=D:/%s/%s" % (os.path.join(q, exe), game["dir"], exe))
    if hl:
        # Half-Life (DOS-GL's tools/halflife): Xash3D's extra data, long file
        # names, a 128 MB PC; demos recorded by DOS-GL's run.sh record
        files += ["%s=D:/HL/VALVE/EXTRAS.PK3" % os.path.join(q, "EXTRAS.PK3"),
                  "%s=D:/HL/DOSLFN.COM" % os.path.join(q, "DOSLFN.COM")]
        cmds.append("DOSLFN")
        extra += ["--mem", "128"]
        for demo in sorted(set(t["demo"] for t in tests.values() if t.get("demo"))):
            src = os.path.join(CACHE, "fixtures", "games", "hldemos", demo.upper() + ".DEM")
            if not os.path.exists(src):
                raise SystemExit("run.py: no %s (DOS-GL: DEMO=%s tools/halflife/run.sh record)" % (src, demo))
            files.append("%s=D:/HL/VALVE/%s.DEM" % (src, demo.upper()))
    if key == "quake2":
        files += ["%s=D:/QUAKE2/BASEQ2/GAMEX86.DXE" % os.path.join(q, "GAMEX86.DXE"),
                  "%s=D:/QUAKE2/DOSLFN.COM" % os.path.join(q, "DOSLFN.COM")]
        cmds.append("DOSLFN")
        for demo in sorted(set(t["demo"] for t in tests.values() if t.get("demo"))):
            src = os.path.join(CACHE, "fixtures", "games", "q2demos", demo.upper() + ".DM2")
            if not os.path.exists(src):
                raise SystemExit("run.py: no %s (DOS-GL: tools/quake/q2record.sh %s)" % (src, demo))
            files.append("%s=D:/QUAKE2/BASEQ2/DEMOS/%s.DM2" % (src, demo.upper()))
    for tid, t in tests.items():
        rsp = os.path.join(rsp_dir, tid + ".RSP")       # DOS command lines stop at 126 characters
        with open(rsp, "w", newline="\r\n") as f:
            f.write(t["args"] + "\n")
        files.append("%s=/TEST/%s.RSP" % (rsp, tid))
        cmds += ["SERSAY HX-START %s" % tid, "%s @C:\\TEST\\%s.RSP" % (t["exe"], tid), "SERSAY HX-DONE 0"]
    cmd = [os.path.join(MGA, "tools", "dev"), "python3", os.path.join(MGA, "tools", "loopa", "run.py"),
           "--name", "dosbench-%s-%s" % (key, card), "--card", card, "--out", out,
           "--games-file", gfile, "--game", key, "--pre", "SET DGL_EXIT_AFTER=0",
           "--pre", "SET DGL_STATS=1",               # a line a second: long timedemos are not idle
           "--timeout", str(a.timeout), "--idle", str(a.idle)] + extra
    for f in files:
        cmd += ["--file", f]
    for c in cmds:
        cmd += ["--cmd", c]
    print("games %s %s: %s" % (card, key, " ".join(tests)))
    subprocess.run(cmd, cwd=ROOT)
    return out


def fw_job(card, tests, a):
    """Fifth Wheel's tests (FIFTHWHEEL: its game and the dgk kit on DOS-GL):
    no retail data, so everything goes on C:. FW1 times 3,000 frames of the
    autopilot driving its generated world; FWP is the performance probe. The game writes its own H
    and T lines (dgk/bench.h)."""
    fw = VARS.get("FIFTHWHEEL", os.path.expanduser("~/FifthWheel"))
    q = rsp_dir = os.path.join(ROOT, "build", "games", "fifthwheel")
    os.makedirs(q, exist_ok=True)
    for src, name in ((os.path.join(fw, "build", "dos", "FWHEEL.EXE"), "FWHEEL.EXE"),
                      (os.path.join(fw, "build", "data", "WORLD.PAK"), "WORLD.PAK")):
        if not os.path.exists(src):
            raise SystemExit("run.py: no %s (Fifth Wheel: make dos)" % src)
        shutil.copyfile(src, os.path.join(q, name))
    out = os.path.join(ROOT, "out", "games", card, "fifthwheel")
    files = ["%s=/TEST/FWHEEL.EXE" % os.path.join(q, "FWHEEL.EXE"), "%s=/TEST/WORLD.PAK" % os.path.join(q, "WORLD.PAK")]
    cmds = ["C:", "CD \\TEST"]
    for tid, t in tests.items():
        rsp = os.path.join(rsp_dir, tid + ".RSP")
        with open(rsp, "w", newline="\r\n") as f:
            f.write(t["args"] + "\n")
        files.append("%s=/TEST/%s.RSP" % (rsp, tid))
        cmds += ["SERSAY HX-START %s" % tid, "%s @C:\\TEST\\%s.RSP" % (t["exe"], tid), "SERSAY HX-DONE 0"]
    cmd = [os.path.join(MGA, "tools", "dev"), "python3", os.path.join(MGA, "tools", "loopa", "run.py"),
           "--name", "dosbench-fifthwheel-%s" % card, "--card", card, "--out", out,
           "--pre", "SET DGL_STATS=1", "--pre", "SET SDL_AUDIO_DRIVER=dummy",
           "--timeout", str(a.timeout), "--idle", str(a.idle)]
    for f in files:
        cmd += ["--file", f]
    for c in cmds:
        cmd += ["--cmd", c]
    print("games %s fifthwheel: %s" % (card, " ".join(tests)))
    subprocess.run(cmd, cwd=ROOT)
    return out


def game_results(out):
    """T records of a game job's RESULTS.TXT, by test ID."""
    path = os.path.join(out, "files", "RESULTS.TXT")
    recs = {}
    if os.path.exists(path):
        for line in open(path, errors="replace"):
            if line.startswith("T "):
                kv = dict(f.split("=", 1) for f in line.split()[1:] if "=" in f)
                recs[kv.get("test", "?")] = kv
    return recs


def cmd_games(a):
    cat = game_catalogue(a.tests)
    cards = a.card.split(",")
    dirs, ok = {}, True
    all_run = {}
    for card in cards:
        have = dosgl_features(card)
        runnable = {k: v for k, v in cat.items() if not v.get("needs") or v["needs"] in have}
        all_run.update(runnable)
        for k in sorted(set(cat) - set(runnable)):
            print("  %-5s %-6s skip why=no-%s" % (card, k, cat[k]["needs"]))
        for key in sorted(set(t["game"] for t in runnable.values())):
            tests = {k: v for k, v in runnable.items() if v["game"] == key}
            out = fw_job(card, tests, a) if key == "fifthwheel" else games_job(card, key, tests, a)
            dirs.setdefault(key, []).append(out)
            recs = game_results(out)
            for tid in tests:
                r = recs.get(tid)
                st = r.get("status", "?") if r else "MISSING"
                ok = ok and st == "ok"
                print("  %-5s %-6s %-7s %s" % (card, tid, st, "fps=%s frames=%s crc=%s" % (r["fps"], r["frames"], r["crc"])
                                                               if r else ""))
    results = []
    for key, kd in dirs.items():
        for d in kd:                                     # variants against their plain runs
            l = shots(d, "l")
            for tid, t in all_run.items():
                if t.get("ref") and tid in l and t["ref"] in l:
                    kind = "game-mtex-vs-2pass" if t.get("needs") == "mtex" else "game-pal-vs-rgba"
                    image_check(results, l[t["ref"]], l[tid], kind, True, tid)
        base = shots(kd[0], "l")                         # every card against the first
        for d in kd[1:]:
            other = shots(d, "l")
            for tid in sorted(base):
                if tid in other:
                    image_check(results, base[tid], other[tid], "game-across-cards", True, tid)
    first = next(iter(dirs.values()))[0] if dirs else os.path.join(ROOT, "out", "games")
    ok = report_checks([first], results) and ok
    print("games: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def cmd_compare(a):
    return 0 if report_checks([os.path.abspath(d) for d in a.dirs]) else 1


def cmd_bench(a):
    """A bench job through MGA-Glide's tools/bench/run.py: both programs back to
    back (OpenGL, then Glide on the job's GLIDE2X.OVL), results uploaded from
    C:\\OUT. --install-data instead copies the scenes to C:\\DOSBENCH\\DATA once."""
    bench = [sys.executable, os.path.join(MGA, "tools", "bench", "run.py"), "--pc", a.pc,
             "--timeout", str(a.timeout)]
    if a.install_data:
        cmd = bench + ["--name", "dosbench-data", "--extender", "dos4gw"]
        for f in data_files():
            cmd += ["--file", "%s=%s" % (f, os.path.basename(f).upper())]
        cmd += ["--cmd", "IF NOT EXIST C:\\DOSBENCH\\NUL MD C:\\DOSBENCH",
                "--cmd", "IF NOT EXIST C:\\DOSBENCH\\DATA\\NUL MD C:\\DOSBENCH\\DATA",
                "--cmd", "COPY C:\\TEST\\*.DBS C:\\DOSBENCH\\DATA > NUL",
                "--cmd", "DIR C:\\DOSBENCH\\DATA > C:\\OUT\\DATA.TXT",
                "--cmd", "SERSAY HX-DONE 0"]
    else:
        common = "--tests %s --data C:\\DOSBENCH\\DATA --secs %s" % (a.tests, a.secs)
        if a.modes:
            common += " --modes " + a.modes
        if a.shots:
            common += " --shots"
        if a.submit:
            common += " --submit " + a.submit
        cmd = bench + ["--name", "dosbench",
                       "--file", os.path.join(ROOT, "build/dos/BENCHG.EXE"),
                       "--file", os.path.join(ROOT, "build/dos/BENCHGL.EXE"),
                       "--ovl", os.path.join(MGA, "build", "ow", "GLIDE2X.OVL"),
                       "--cmd", "BENCHGL.EXE %s" % common,
                       "--cmd", "BENCHG.EXE %s --glide=C:\\TEST\\GLIDE2X.OVL" % common]
    print(" ".join(cmd), flush=True)
    rc = subprocess.run(cmd, cwd=MGA).returncode
    if not a.install_data:
        jobs = sorted(glob.glob(os.path.join(MGA, "out", "bench", a.pc, "dosbench-*")), key=os.path.getmtime)
        if jobs:
            fill_glide_display(jobs[-1])
        if jobs and os.path.exists(os.path.join(jobs[-1], "files", "RESULTS.TXT")):
            print("results: %s (python3 tools/report.py ingest %s --pc %s)" % (jobs[-1], jobs[-1], a.pc))
    return rc


WINVM_NOTES = """What is on C:
  C:\\DOSBENCH   DOSBench. Type DOSBENCH for the menu (it starts in this directory):
                pick APIs, modes and tests, R runs them, V shows the results.
                BENCHGL.EXE is OpenGL on DOS-GL, BENCHG.EXE Glide on MGA-Glide's
                GLIDE2X.OVL (this directory). The scenes are in DATA; see
                DATA\\CREDITS.TXT (the Stanford scans are for research use: this is
                a private copy, do not pass it on).
  C:\\DOSGL      DOS-GL demos: TRI, CUBE, TEXCUBE (add --frames 600 to watch
                longer), PROBE (what the library found), CLEAR.
  C:\\CC         ClassiCube on DOS-GL, with a procedural test texture pack:
                CD \\CC, then CCDOS --singleplayer (Esc, then Quit to leave).
  C:\\HX         DOS4GW.EXE, CWSDPMI.EXE and small helpers (on PATH).

Timings inside 86Box describe the emulator, not the hardware.
The Glide programs use MGA-Glide on the Matrox card. To see 3dfx's own
runtime on the emulated Voodoo, copy a retail GLIDE2X.OVL of your own in
and run BENCHG --glide=THATFILE (it is not included).
"""

QUAKE_NOTES = """
What is on D: (the Quakes in DOS-GL's builds, on the Matrox card; D:\\ is on PATH)
  QUAKE         Quake (retail; your copy) in GLQuake (qdos). Options go to the
                game: -mtex (multitexture: G400/G450), -8bit (paletted
                textures), +timedemo demo1 (or demo2, demo3).
  LQ            the same engine on LibreQuake's free data.
  QUAKE2        Quake 2 (retail; your copy). Options go to the game:
                +set gl_ext_multitexture 1 (G400/G450; off by default in the
                DOS port), +set gl_ext_palettedtexture 1,
                +set timedemo 1 +demomap q2bench1.dm2 (a demo you recorded).
  The games start with sound enabled on the Sound Blaster 16 (BLASTER is set
  at boot; add -nosound, or +set s_initsound 0 for Quake 2, to turn it off).
  The games are in
  D:\\QUAKE, D:\\LQ and D:\\QUAKE2 (with CWSDPMI from C:\\HX). This is retail
  data: the zip is for your own machine only.
"""

# The Quakes on D: for --games: (fixture dir, D: directory, launcher, title,
# the game's command line).
QUAKES = [("quake", "QUAKE", "QUAKE", "Quake (retail) in GLQuake on DOS-GL",
           "QDOSDGL.EXE -width 640 -height 480 -nocdaudio -nolan"),
          ("lq", "LQ", "LQ", "LibreQuake in GLQuake on DOS-GL",
           "QDOSDGL.EXE -width 640 -height 480 -nocdaudio -nolan"),
          ("quake2", "QUAKE2", "QUAKE2", "Quake 2 (retail) on DOS-GL",
           "Q2DGL.EXE")]


def quake_disk(dosgl, stage):
    """mkwinvm --d-dir/--d-file arguments putting the Quakes on D:, or [] when
    DOS-GL's builds or the fixtures are missing."""
    q = os.path.join(dosgl, "build", "quake")
    fix = os.path.join(CACHE, "fixtures", "games")
    need = [os.path.join(q, f) for f in ("QDOSDGL.EXE", "Q2DGL.EXE", "GAMEX86.DXE", "DOSLFN.COM")]
    need += [os.path.join(fix, g) for g, *_ in QUAKES]
    missing = [p for p in need if not os.path.exists(p)]
    if missing:
        print("winvm: no Quakes (missing %s)" % ", ".join(missing))
        return []
    def staged(src):
        dst = os.path.join(stage, "quake-" + os.path.basename(src))
        shutil.copyfile(src, dst)
        return dst
    args = []
    for g, ddir, bat, title, cmd in QUAKES:
        args += ["--d-dir", "%s=/%s" % (os.path.join(fix, g), ddir)]
        lines = ["@ECHO OFF", "REM %s [options]: %s" % (bat, title), "D:", "CD \\" + ddir]
        if g == "quake2":
            lines.append("DOSLFN > NUL")
        lines += [cmd + " %1 %2 %3 %4 %5 %6 %7 %8 %9", "C:", "CD \\"]
        path = os.path.join(stage, bat + ".BAT")
        open(path, "w", newline="").write("\r\n".join(lines) + "\r\n")
        args += ["--d-file", "%s=/%s.BAT" % (path, bat)]
        exe = cmd.split()[0]
        args += ["--d-file", "%s=/%s/%s" % (staged(os.path.join(q, exe)), ddir, exe)]
    args += ["--d-file", "%s=/QUAKE2/BASEQ2/GAMEX86.DXE" % staged(os.path.join(q, "GAMEX86.DXE")),
             "--d-file", "%s=/QUAKE2/DOSLFN.COM" % staged(os.path.join(q, "DOSLFN.COM"))]
    for dm in sorted(glob.glob(os.path.join(fix, "q2demos", "*.DM2"))):
        args += ["--d-file", "%s=/QUAKE2/BASEQ2/DEMOS/%s" % (dm, os.path.basename(dm))]
    return args + ["--d-cylinders", "1023"]


def cmd_winvm(a):
    """A ready-to-boot 86Box machine with DOSBench, DOS-GL's demos and
    ClassiCube, for MGA-Glide's patched 86Box (Windows kit or Linux)."""
    dosgl = VARS["DOSGL"]
    name = "dosbench-" + a.card + ("-games" if a.games else "")
    files = ["%s=/DOSBENCH/%s" % (os.path.join(ROOT, "build/dos", f), f)
             for f in ("BENCHG.EXE", "BENCHGL.EXE", "DBMENU.EXE")]
    files += ["%s=/DOSBENCH/DOSBENCH.BAT" % os.path.join(ROOT, "dos/DOSBENCH.BAT"),
              "%s=/DOSBENCH/GLIDE2X.OVL" % os.path.join(MGA, "build/ow/GLIDE2X.OVL")]
    for f in data_files() + [os.path.join(ROOT, "build/data/CREDITS.TXT")]:
        if os.path.exists(f):
            files.append("%s=/DOSBENCH/DATA/%s" % (f, os.path.basename(f).upper()))
    for f in ("TRI", "CUBE", "TEXCUBE", "PROBE", "CLEAR"):
        p = os.path.join(dosgl, "build/exe/%s.EXE" % f)
        if os.path.exists(p):
            files.append("%s=/DOSGL/%s.EXE" % (p, f))
    cc = os.path.join(dosgl, "build/cc/CCDOS.EXE")
    if os.path.exists(cc):
        files += ["%s=/CC/CCDOS.EXE" % cc,
                  "%s=/CC/TEXPACKS/DEFAULT.ZIP" % os.path.join(dosgl, "build/cc/default.zip")]
    # The dev container mounts MGA-Glide and this repository only: stage the rest here.
    stage = os.path.join(ROOT, "build", "winvm")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    staged = []
    for i, spec in enumerate(files):
        src, dst = spec.split("=", 1)
        if not os.path.abspath(src).startswith((ROOT + os.sep, MGA + os.sep)):
            copy = os.path.join(stage, "%02d-%s" % (i, os.path.basename(src)))
            shutil.copyfile(src, copy)
            src = copy
        staged.append("%s=%s" % (src, dst))
    files = staged
    quake = quake_disk(dosgl, stage) if a.games else []
    notes = os.path.join(ROOT, "build", "winvm-notes.txt")
    open(notes, "w").write(WINVM_NOTES + (QUAKE_NOTES if quake else ""))
    out = os.path.join(ROOT, "dist", name + "-vm.zip")
    cmd = [os.path.join(MGA, "tools", "dev"), "python3", os.path.join(MGA, "tools", "86box", "mkwinvm.py"),
           "--name", name, "--card", a.card, "--readme", notes, "--out", out,
           "--run", "CD \\DOSBENCH", "--run", "ECHO DOSBench: type DOSBENCH for the menu (README.txt has the rest)."]
    for f in files:
        cmd += ["--file", f]
    if a.games:
        # Retail games from MGA-Glide's local fixtures: this zip is for the owner's machine only.
        cmd += ["--game", "gta", "--game", "sr", "--mga-ovl", os.path.join(MGA, "build/ow/GLIDE2X.OVL")] + quake
    return subprocess.run(cmd, cwd=ROOT).returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="what", required=True)
    p = sub.add_parser("loopa")
    p.add_argument("--card", default="g450")
    p.add_argument("--tests", default="all")
    p.add_argument("--modes", default="")
    p.add_argument("--no-ref", dest="ref", action="store_false")
    p.add_argument("--full", action="store_true", help="full frame counts (slow in 86Box)")
    p.add_argument("--timeout", type=int, default=3600)
    p.add_argument("--idle", type=int, default=300)
    p = sub.add_parser("games")
    p.add_argument("--card", default="g450")
    p.add_argument("--tests", default="all")
    p.add_argument("--timeout", type=int, default=7200)
    p.add_argument("--idle", type=int, default=300)
    p = sub.add_parser("compare")
    p.add_argument("dirs", nargs="+")
    p = sub.add_parser("bench")
    p.add_argument("--pc", required=True)
    p.add_argument("--tests", default="all")
    p.add_argument("--modes", default="")
    p.add_argument("--submit", default="")
    p.add_argument("--shots", action="store_true")
    p.add_argument("--secs", default="5", help="target seconds per timed test")
    p.add_argument("--install-data", action="store_true", help="copy the scenes to C:\\DOSBENCH\\DATA (once per PC)")
    p.add_argument("--timeout", type=int, default=3600)
    p = sub.add_parser("winvm")
    p.add_argument("--card", default="g450", choices=["g100", "g200", "g400", "g450"])
    p.add_argument("--games", action="store_true",
                   help="also install GTA, Screamer Rally and the Quakes from the local fixtures (private zip)")
    a = ap.parse_args()
    return {"loopa": cmd_loopa, "games": cmd_games, "compare": cmd_compare, "bench": cmd_bench,
            "winvm": cmd_winvm}[a.what](a)


if __name__ == "__main__":
    sys.exit(main())
