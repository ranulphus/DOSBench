#!/usr/bin/env python3
"""DOSBench results: ingest measurement records, print tables, render a page.

  report.py ingest DIR [DIR...] [--source bench|loopa] [--pc NAME]
      Read DIR/files/RESULTS.TXT (a Loop A or bench job's output) or
      DIR/RESULTS.TXT and append its records to results/<pc>.jsonl (one JSON
      object per test result, header fields merged in; duplicates skipped).
  report.py table [FILES...] [--latest] [--mode 640x480] [--include-loopa]
      Text tables: one row per test, one column per target (pc, api, impl).
  report.py html [FILES...] [--out out/report.html] [--include-loopa]
      A self-contained page: per-group tables, bar charts comparing targets,
      runs over time, and derived figures (state-change cost). Publish it as
      a private page when asked; nothing leaves the machine otherwise.

Records from Loop A describe the emulator, not hardware: they are kept
apart (source=loopa) and left out of tables and pages unless asked for.
"""
import argparse
import glob
import html
import json
import os
import re
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(ROOT, "results")

NUMERIC = re.compile(r"^-?\d+(\.\d+)?$")

# The figure a test is judged by, per test-id prefix (longest match wins).
PRIMARY = [
    ("S1", "mpix_s", "Mpixels/s"),
    ("S2", "ktris_s", "Ktris/s"),
    ("S3UPL", "mtexel_s", "Mtexels/s"),
    ("S3SUB", "mtexel_s", "Mtexels/s"),
    ("S3WS", "fps", "fps"),
    ("S4", "ktris_s", "Ktris/s"),
    ("M", "fps", "fps"),
    ("L", "fps", "fps"),
    ("B0", "fps", "fps"),
    ("Q1", "fps", "fps"),
    ("Q2", "fps", "fps"),
]
GROUPS = [("basic", "Basic"), ("synth", "Synthetic"), ("model", "Models"), ("level", "Level"), ("game", "Games")]


def primary(test):
    best = ("fps", "fps", 0)
    for prefix, key, label in PRIMARY:
        if test.startswith(prefix) and len(prefix) > best[2]:
            best = (key, label, len(prefix))
    return best[0], best[1]


def parse_line(line):
    parts = line.split()
    if not parts or parts[0] not in ("H", "T"):
        return None
    rec = {"kind": parts[0]}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            rec[k] = float(v) if NUMERIC.match(v) and k not in ("run", "crc", "build") else v
    return rec


def read_results(path):
    heads, tests = {}, []
    for line in open(path, encoding="latin-1"):
        rec = parse_line(line.strip())
        if not rec:
            continue
        if rec["kind"] == "H":
            heads[(rec.get("run"), rec.get("mode"))] = rec
        else:
            tests.append(rec)
    out = []
    for t in tests:
        h = heads.get((t.get("run"), t.get("mode")), {})
        rec = {k: v for k, v in h.items() if k not in ("kind",)}
        rec.update({k: v for k, v in t.items() if k != "kind"})
        out.append(rec)
    return out


def load(files):
    recs = []
    for f in files:
        for line in open(f):
            line = line.strip()
            if line:
                recs.append(json.loads(line))
    return recs


def default_files():
    return sorted(glob.glob(os.path.join(RESULTS, "*.jsonl")))


def target(r):
    """A column: where and how the numbers were taken."""
    where = r.get("pc") or "?"
    api = "Glide" if r.get("api") == "glide" else "OpenGL"
    impl = r.get("impl", "")
    if r.get("tag") == "V":
        impl = "3dfx " + impl
    elif r.get("api") == "glide":
        impl = "MGA-Glide " + impl
    sub = r.get("submit", "arrays")
    if r.get("api") == "opengl" and sub != "arrays":
        api += "/" + sub
    return "%s | %s | %s" % (where, api, impl.replace("_", " "))


# ---- ingest -------------------------------------------------------------
def cmd_ingest(a):
    os.makedirs(RESULTS, exist_ok=True)
    total = 0
    for d in a.dirs:
        path = None
        for cand in (os.path.join(d, "files", "RESULTS.TXT"), os.path.join(d, "RESULTS.TXT"), d):
            if os.path.isfile(cand):
                path = cand
                break
        if not path:
            print("report: no RESULTS.TXT under %s" % d)
            continue
        pc = a.pc or os.path.basename(os.path.normpath(d if not os.path.isfile(d) else os.path.dirname(d)))
        if pc == "files":
            pc = os.path.basename(os.path.dirname(os.path.normpath(os.path.dirname(path))))
        recs = read_results(path)
        stamp = time.strftime("%Y-%m-%d %H:%M", time.localtime(os.path.getmtime(path)))
        base = re.sub(r"[^A-Za-z0-9_.-]", "_", pc)
        if a.source == "loopa" and not base.startswith("loopa-"):
            base = "loopa-" + base          # emulator records: kept apart, not committed
        out = os.path.join(RESULTS, "%s.jsonl" % base)
        seen = set()
        if os.path.exists(out):
            for r in load([out]):
                seen.add((r.get("run"), r.get("mode"), r.get("test")))
        n = 0
        with open(out, "a") as f:
            for r in recs:
                key = (r.get("run"), r.get("mode"), r.get("test"))
                if key in seen:
                    continue
                r.update(source=a.source, pc=pc, when=stamp)
                f.write(json.dumps(r, sort_keys=True) + "\n")
                seen.add(key)
                n += 1
        print("report: %s -> %s (%d new records)" % (path, os.path.relpath(out, ROOT), n))
        total += n
    return 0


# ---- selection ------------------------------------------------------------
def select(recs, a):
    recs = [r for r in recs if r.get("status") == "ok"]
    if not a.include_loopa:
        recs = [r for r in recs if r.get("source") != "loopa"]
    if a.mode:
        recs = [r for r in recs if r.get("mode") == a.mode]
    return recs


def latest_by(recs):
    """The newest record per (target, mode, test)."""
    best = {}
    for r in recs:
        k = (target(r), r.get("mode"), r.get("test"))
        if k not in best or r.get("when", "") >= best[k].get("when", ""):
            best[k] = r
    return best


def derived(best, tgt, mode):
    """State-change cost: (T or B frame time - D frame time) / changes, in microseconds."""
    out = {}
    for k in ("1", "16"):
        d = best.get((tgt, mode, "S4D" + k))
        for what in ("T", "B"):
            r = best.get((tgt, mode, "S4%s%s" % (what, k)))
            if d and r:
                changes = float(str(r.get("changes", 0)) or 0)
                if changes:
                    out["S4%s%s" % (what, k)] = (r["avg_ms"] - d["avg_ms"]) * 1000.0 / changes
    return out


def shown_in(best, mode):
    """' (shown in 640x480: integer)' when a size was scaled or zoomed into a
    larger BIOS mode (H line display= fit=, from 2026-09); else ''."""
    seen = sorted({(r.get("display"), r.get("fit")) for k, r in best.items() if k[1] == mode
                   and r.get("fit") not in (None, "", "?", "native")})
    return "" if not seen else " (shown in " + ", ".join("%s: %s" % (d or "?", f) for d, f in seen) + ")"


def matrix(recs):
    best = latest_by(recs)
    targets = sorted({k[0] for k in best})
    def area(m):
        w, _, h = (m or "0x0").partition("x")
        return (int(w) * int(h) if w.isdigit() and h.isdigit() else 0, m or "")
    modes = sorted({k[1] for k in best}, key=area)
    tests = []
    for k in best:
        if k[2] not in tests:
            tests.append(k[2])
    order = {g: i for i, (g, _) in enumerate(GROUPS)}
    group_of = {r.get("test"): r.get("group") for r in best.values()}
    tests.sort(key=lambda t: (order.get(group_of.get(t), 9), t))
    return best, targets, modes, tests, group_of


# ---- table ----------------------------------------------------------------
def fmt(v):
    if v is None:
        return "-"
    return "%.0f" % v if v >= 100 else "%.1f" % v if v >= 10 else "%.2f" % v


def cmd_table(a):
    recs = select(load(a.files or default_files()), a)
    if not recs:
        print("report: no records (use --include-loopa for Loop A runs)")
        return 1
    best, targets, modes, tests, _ = matrix(recs)
    for mode in modes:
        print("\n== %s%s" % (mode, shown_in(best, mode)))
        for i, t in enumerate(targets):
            print("  [%d] %s" % (i + 1, t))
        print("  %-8s %-10s " % ("test", "metric") + " ".join("%10s" % ("[%d]" % (i + 1)) for i in range(len(targets))))
        for test in tests:
            key, label = primary(test)
            row = [best.get((t, mode, test), {}).get(key) for t in targets]
            if all(v is None for v in row):
                continue
            print("  %-8s %-10s " % (test, label) + " ".join("%10s" % fmt(v) for v in row))
        for ti, t in enumerate(targets):
            d = derived(best, t, mode)
            if d:
                print("  state-change cost [%d]: " % (ti + 1) + ", ".join("%s %.2f us" % (k, v) for k, v in sorted(d.items())))
    return 0


# ---- html -----------------------------------------------------------------
CSS = """
:root { --bg:#fbfaf7; --fg:#1d1d1b; --muted:#6b6a64; --line:#dddad2; --card:#ffffff;
        --c1:#2f6db3; --c2:#c4572a; --c3:#3d8b4f; --c4:#8a5bb0; --c5:#b38f2f; --c6:#2a9aa0; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
        --bg:#171716; --fg:#ecebe6; --muted:#a09e96; --line:#34332f; --card:#1f1f1d;
        --c1:#6ea3e0; --c2:#e7865c; --c3:#72bd83; --c4:#b692d8; --c5:#dcbc62; --c6:#5fc8cd; } }
:root[data-theme="dark"] { --bg:#171716; --fg:#ecebe6; --muted:#a09e96; --line:#34332f; --card:#1f1f1d;
        --c1:#6ea3e0; --c2:#e7865c; --c3:#72bd83; --c4:#b692d8; --c5:#dcbc62; --c6:#5fc8cd; }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--fg); font:15px/1.5 system-ui, sans-serif; }
main { max-width: 1100px; margin: 0 auto; padding: 24px 16px 64px; }
h1 { font-size: 26px; margin: 0 0 4px; } h2 { font-size: 19px; margin: 36px 0 8px; }
h3 { font-size: 15px; margin: 20px 0 6px; color: var(--muted); font-weight: 600; }
p.lede { color: var(--muted); margin: 0 0 20px; }
.targets { list-style: none; padding: 0; margin: 0 0 8px; display: grid; gap: 4px; }
.targets li { display: flex; gap: 8px; align-items: center; font-size: 14px; }
.sw { width: 12px; height: 12px; border-radius: 3px; flex: none; }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; background: var(--card); font-size: 13.5px; }
th, td { padding: 5px 8px; border-bottom: 1px solid var(--line); text-align: right; white-space: nowrap; }
th:first-child, td:first-child, th:nth-child(2), td:nth-child(2) { text-align: left; }
thead th { color: var(--muted); font-weight: 600; }
td.what { color: var(--muted); white-space: normal; min-width: 180px; }
svg text { fill: var(--fg); font-size: 11px; } svg .muted { fill: var(--muted); }
.note { color: var(--muted); font-size: 13px; }
"""
COLOURS = ["var(--c1)", "var(--c2)", "var(--c3)", "var(--c4)", "var(--c5)", "var(--c6)"]


def bars(test, label, values, width=520):
    """Horizontal bars, one per target."""
    vals = [(i, v) for i, v in enumerate(values) if v is not None]
    if not vals:
        return ""
    top = max(v for _, v in vals) or 1
    rowh, left, top_pad = 18, 8, 18
    h = rowh * len(values) + 8 + top_pad
    out = ['<svg viewBox="0 0 %d %d" width="100%%" style="max-width:%dpx" role="img" aria-label="%s">' %
           (width, h, width, html.escape("%s %s" % (test, label)))]
    for i, v in enumerate(values):
        y = top_pad + 4 + i * rowh
        if v is None:
            out.append('<text x="%d" y="%d" class="muted">n/a</text>' % (left, y + 12))
            continue
        w = max(1, (width - left - 70) * v / top)
        out.append('<rect x="%d" y="%d" width="%.1f" height="%d" rx="2" fill="%s"/>' %
                   (left, y, w, rowh - 5, COLOURS[i % len(COLOURS)]))
        out.append('<text x="%.1f" y="%d">%s</text>' % (left + w + 4, y + 11, fmt(v)))
    out.append('<text x="0" y="%d" class="muted">%s</text>' % (12, html.escape(test)))
    out.append("</svg>")
    return "".join(out)


def history(recs, test, key, targets, width=520, height=120):
    """Primary figure over time per target (only drawn with two or more runs)."""
    series = {}
    for r in recs:
        if r.get("test") == test and key in r:
            series.setdefault(target(r), []).append((r.get("when", ""), r[key]))
    series = {t: sorted(v) for t, v in series.items() if len(v) > 1}
    if not series:
        return ""
    whens = sorted({w for v in series.values() for w, _ in v})
    top = max(val for v in series.values() for _, val in v) or 1
    out = ['<svg viewBox="0 0 %d %d" width="100%%" style="max-width:%dpx" role="img">' % (width, height, width)]
    for t, pts in series.items():
        i = targets.index(t) if t in targets else 0
        xy = ["%.1f,%.1f" % (10 + (width - 20) * whens.index(w) / max(1, len(whens) - 1),
                             height - 10 - (height - 24) * val / top) for w, val in pts]
        out.append('<polyline fill="none" stroke="%s" stroke-width="2" points="%s"/>' % (COLOURS[i % len(COLOURS)],
                                                                                          " ".join(xy)))
    out.append('<text x="10" y="12" class="muted">%s over time</text></svg>' % html.escape(test))
    return "".join(out)


def cmd_html(a):
    recs = select(load(a.files or default_files()), a)
    if not recs:
        print("report: no records (use --include-loopa for Loop A runs)")
        return 1
    best, targets, modes, tests, group_of = matrix(recs)
    body = ["<h1>DOSBench results</h1>",
            '<p class="lede">Glide 2.x and OpenGL 1.1 under DOS: the same tests through each API. '
            "Latest run per target; %d records, generated %s.%s</p>" %
            (len(recs), time.strftime("%Y-%m-%d %H:%M"),
             " Includes Loop A (86Box) runs: those describe the emulator, not hardware." if a.include_loopa else "")]
    body.append('<ul class="targets">' + "".join(
        '<li><span class="sw" style="background:%s"></span>%s</li>' % (COLOURS[i % len(COLOURS)], html.escape(t))
        for i, t in enumerate(targets)) + "</ul>")
    for mode in modes:
        body.append("<h2>%s</h2>" % html.escape(mode + shown_in(best, mode)))
        for g, gname in GROUPS:
            gtests = [t for t in tests if group_of.get(t) == g and any((tg, mode, t) in best for tg in targets)]
            if not gtests:
                continue
            body.append("<h3>%s</h3>" % gname)
            rows = []
            for t in gtests:
                key, label = primary(t)
                cells = []
                for tg in targets:
                    r = best.get((tg, mode, t))
                    cells.append("<td>%s</td>" % ("%s<br><span class=note>%s fps, p99 %s ms</span>" %
                                                  (fmt(r.get(key)), fmt(r.get("fps")), fmt(r.get("p99_ms")))
                                                  if r else "-"))
                rows.append("<tr><td>%s</td><td>%s</td>%s</tr>" % (html.escape(t), html.escape(label), "".join(cells)))
            head = "".join("<th>[%d]</th>" % (i + 1) for i in range(len(targets)))
            body.append('<div class="scroll"><table><thead><tr><th>test</th><th>metric</th>%s</tr></thead>'
                        "<tbody>%s</tbody></table></div>" % (head, "".join(rows)))
            for t in gtests:
                key, label = primary(t)
                body.append(bars("%s (%s)" % (t, label), label,
                                 [best.get((tg, mode, t), {}).get(key) for tg in targets]))
                body.append(history([r for r in recs if r.get("mode") == mode], t, key, targets))
        costs = [(tg, derived(best, tg, mode)) for tg in targets]
        if any(c for _, c in costs):
            body.append("<h3>State-change cost (microseconds per change)</h3>")
            body.append('<p class="note">Frame time with a change every k triangles, less the frame time with '
                        "a draw call every k triangles and no change, divided by the number of changes.</p>")
            keys = sorted({k for _, c in costs for k in c})
            rows = "".join("<tr><td>%s</td><td>us</td>%s</tr>" % (k, "".join(
                "<td>%s</td>" % (fmt(c.get(k)) if k in c else "-") for _, c in costs)) for k in keys)
            head = "".join("<th>[%d]</th>" % (i + 1) for i in range(len(targets)))
            body.append('<div class="scroll"><table><thead><tr><th>test</th><th>unit</th>%s</tr></thead>'
                        "<tbody>%s</tbody></table></div>" % (head, rows))
    page = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width, initial-scale=1">'
            "<title>DOSBench Results</title><style>%s</style></head><body><main>%s</main></body></html>" %
            (CSS, "\n".join(body)))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, "w").write(page)
    print("report: %s (%d targets, %d tests)" % (os.path.relpath(a.out, ROOT), len(targets), len(tests)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="what", required=True)
    p = sub.add_parser("ingest")
    p.add_argument("dirs", nargs="+")
    p.add_argument("--source", default="bench", choices=["bench", "loopa"])
    p.add_argument("--pc")
    for name in ("table", "html"):
        p = sub.add_parser(name)
        p.add_argument("files", nargs="*")
        p.add_argument("--mode")
        p.add_argument("--include-loopa", action="store_true")
        if name == "html":
            p.add_argument("--out", default=os.path.join(ROOT, "out", "report.html"))
    a = ap.parse_args()
    return {"ingest": cmd_ingest, "table": cmd_table, "html": cmd_html}[a.what](a)


if __name__ == "__main__":
    sys.exit(main())
