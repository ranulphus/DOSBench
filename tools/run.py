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

  run.py winvm [--card g450]
      A ready-to-boot 86Box machine (dist/dosbench-<card>-vm.zip) with DOSBench,
      DOS-GL's demos and ClassiCube, for MGA-Glide's patched 86Box, including
      its Windows build (MGA-Glide tools/86box/windows/).

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
    for k in ("MGA_GLIDE", "DOSGL"):
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
    files = ["%s=/TEST/BENCHG.EXE" % os.path.join(ROOT, "build/dos/BENCHG.EXE"),
             "%s=/TEST/BENCHGL.EXE" % os.path.join(ROOT, "build/dos/BENCHGL.EXE")]
    for f in data_files():
        files.append("%s=/DOSBENCH/DATA/%s" % (f, os.path.basename(f).upper()))
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


def compare_images(dirs):
    """Frame checks within and across Loop A output directories."""
    import imgcmp
    cfg = checks_config()
    default = cfg.get("default", {"tol": 24, "frac": 0.005})
    results = []

    def cmp(ref, got, kind, gate, test):
        opts = dict(default)
        opts.update(cfg.get(kind, {}).get("default", {}))
        opts.update(cfg.get(kind, {}).get(test, {}))
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
        return {os.path.basename(p)[1:-4].upper(): p for p in glob.glob(os.path.join(d, tag.lower() + "*.png"))}

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


def cmd_loopa(a):
    cards = a.card.split(",")
    a.ref_card = cards[0]
    dirs, all_ok = [], True
    for card in cards:
        out = loopa_job(card, a)
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


def report_checks(dirs):
    results = compare_images(dirs)
    gated_bad = [r for r in results if r["gate"] and not r["ok"]]
    for r in results:
        flag = "ok " if r["ok"] else ("BAD" if r["gate"] else "adv")
        print("  %s %-16s %-8s frac=%-8s worst=%-3s %s" % (flag, r["kind"], r["test"], r.get("frac"), r.get("worst"),
                                                        r["got"]))
    json.dump(results, open(os.path.join(dirs[0], "checks.json"), "w"), indent=1)
    print("image checks: %d compared, %d gating failures" % (len(results), len(gated_bad)))
    return not gated_bad


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


def cmd_winvm(a):
    """A ready-to-boot 86Box machine with DOSBench, DOS-GL's demos and
    ClassiCube, for MGA-Glide's patched 86Box (Windows kit or Linux)."""
    dosgl = VARS["DOSGL"]
    name = "dosbench-" + a.card
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
    notes = os.path.join(ROOT, "build", "winvm-notes.txt")
    open(notes, "w").write(WINVM_NOTES)
    out = os.path.join(ROOT, "dist", name + "-vm.zip")
    cmd = [os.path.join(MGA, "tools", "dev"), "python3", os.path.join(MGA, "tools", "86box", "mkwinvm.py"),
           "--name", name, "--card", a.card, "--readme", notes, "--out", out,
           "--run", "CD \\DOSBENCH", "--run", "ECHO DOSBench: type DOSBENCH for the menu (README.txt has the rest)."]
    for f in files:
        cmd += ["--file", f]
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
    a = ap.parse_args()
    return {"loopa": cmd_loopa, "compare": cmd_compare, "bench": cmd_bench, "winvm": cmd_winvm}[a.what](a)


if __name__ == "__main__":
    sys.exit(main())
