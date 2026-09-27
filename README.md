# DOSBench

A 3D benchmark for DOS that draws the same tests and scenes through
**Glide 2.x** and **OpenGL 1.1**, so the two can be compared on the same PC
and card, and Glide on a Matrox card (through MGA-Glide) against Glide on a
Voodoo. It is also the measuring tool for MGA-Glide and DOS-GL's performance
goals on the Pentium II bench.

| Program | Built with | Draws through |
|---|---|---|
| `BENCHG.EXE` | Open Watcom, DOS/4GW | Glide 2.x: MGA-Glide's `GLIDE2X.OVL` on a G100-G450, or 3dfx's on a Voodoo |
| `BENCHGL.EXE` | DJGPP, CWSDPMI | OpenGL 1.1: DOS-GL's `libGL.a` on a G200/G400/G450 |
| `DBMENU.EXE` + `DOSBENCH.BAT` | DJGPP | a text-mode menu that runs the two and shows their results |

Tests (docs/tests.md): fill rate, triangle rate, texture upload and
working set, state changes; model scenes (the Utah teapot at three
tessellations, the Stanford bunny and dragon, three Khronos glTF samples);
and a LibreQuake fly-through with Quake's visibility, lightmaps in two passes
or baked, fences, flame sprites and water. Frame times come from the TSC
and are reported as averages, medians and 99th percentiles
(docs/methodology.md).

**Status:** everything runs in 86Box (Loop A) on the emulated G450, G400 and
G200 and on the emulated Voodoo with 3dfx's runtime, with frames checked
across them; the virtual bench PC runs a bench job end to end. Real
hardware numbers come from the bench, not yet provisioned.

```
make && make tests-host
make data                                  # fetch and convert the scenes
python3 tools/run.py loopa --card g450     # both programs in 86Box, frames checked
python3 tools/run.py bench --pc bench-g450 # the real numbers (docs/running.md)
python3 tools/report.py html               # comparison page
```

See docs/running.md for building, the menu, Loop A, the bench and reports,
and docs/content.md for where the scene data comes from.

## Licence

MIT (`LICENSE`) for DOSBench's own code. The scene data is not in this
repository: `tools/assets.py` fetches it from its publishers under their own
terms (docs/content.md), including the Stanford scans, which are for
research use and are never redistributed.
