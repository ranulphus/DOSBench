# Building and running

## Building

DOSBench builds against sibling checkouts of MGA-Glide (the Glide loader,
the test shim and the shared HAL, the Loop A and bench harnesses) and DOS-GL
(`libGL.a`), at or after the commits pinned in `deps.mk`
(`config.mk` has the paths; override them in `config.local.mk`).

```
make                      # build/dos/BENCHG.EXE, BENCHGL.EXE, DBMENU.EXE
make tests-host           # host unit tests
make data                 # fetch the sources, convert build/data/*.DBS (tools/assets.py)
```

`make` first builds what it needs in the sibling checkouts (MGA-Glide's
generated Glide table, HAL library, `GLIDE2X.OVL` and DOS helpers; DOS-GL's
`libGL.a`). The toolchains are MGA-Glide's (`make setup-ow setup-djgpp` there).

## The programs

```
BENCHGL [options]                   OpenGL on DOS-GL (DJGPP; needs CWSDPMI.EXE)
BENCHG  [options] --glide=PATH      Glide via an OVL (DOS/4GW; needs DOS4GW.EXE)

  --tests LIST        IDs, prefixes, groups or all (default)
  --tests-from FILE   the same, read from a file (names separated by commas or lines)
  --args FILE         more arguments from a file (DOS command lines are short)
  --modes WxH,...|all screen modes (default 640x480); all = 320x200, 320x240, 400x300, 512x384,
                      640x480, 640x512, 800x600, 1024x768, 1280x1024, 1600x1200, where a mode
                      the card cannot show is skipped (HX-STAT skip), not failed. Sizes the
                      BIOS lacks are drawn at their size and scaled (or, with DGL_ZOOM=1 /
                      MGAGLIDE zoom=1, zoomed) into a larger BIOS mode; the H line records
                      display=WxH fit=native|zoom|integer|fill|aspect. Glide has no 640x512:
                      run it with SET MGAGLIDE=res=640x512 and --modes 640x480
  --secs S            target seconds per timed test (default 5)
  --submit arrays|lists|immediate   OpenGL static geometry path (default arrays)
  --vsync             sync swaps to the retrace
  --shots             save one frame per test as <out>\<tag><ID>.PPM, in one mode:
                      640x480 when it is among --modes (every card and the Voodoo
                      have it, so the image checks compare like with like), else the first
  --shot-frames F,..  also save these frames (camera paths: along the path), same mode
  --data DIR          scene files (default C:\DOSBENCH\DATA)
  --out DIR           results and frames (default C:\OUT)
  --tag C             image-name prefix (default G or L)
  --quick             a few frames per test (Loop A)
  --list              list the tests
```

Results go to `<out>\RESULTS.TXT` (docs/methodology.md) and COM1.

## The menu

Install into one directory (for example `C:\DOSBENCH`): `BENCHG.EXE`,
`BENCHGL.EXE`, `DBMENU.EXE`, `dos/DOSBENCH.BAT`, `GLIDE2X.OVL` (MGA-Glide's,
or 3dfx's on a Voodoo), `CWSDPMI.EXE`, `DOS4GW.EXE` (or have them on PATH)
and `build/data/*` as `DATA\`. Then `DOSBENCH`:

- choose the APIs, modes, vsync, the OpenGL submission path, seconds per
  test, frame saving and the tests (Space toggles, A all, N none);
- R runs them: the menu writes `TESTS.LST` and `RUNSEL.BAT` and exits, the
  batch file runs each program on its own and returns to the menu;
- V shows `OUT\RESULTS.TXT`: the latest figures per test, mode and program.

## A machine to try it by hand

```
python3 tools/run.py winvm --card g450      # dist/dosbench-g450-vm.zip
python3 tools/run.py winvm --card g450 --games   # dist/dosbench-g450-games-vm.zip
```

A ready-to-boot 86Box machine (Loop A's: Pentium II 350, the emulated card
plus a Voodoo Graphics, Sound Blaster 16, FreeDOS) with DOSBench and its
menu in `C:\DOSBENCH`, DOS-GL's demos in `C:\DOSGL` and ClassiCube in
`C:\CC`; its `README.txt` lists them. It needs MGA-Glide's patched 86Box:
on Linux the one `make 86box` builds, on Windows the one MGA-Glide's
Windows kit builds (`tools/86box/mkwinkit.sh`, then `build-windows.sh` in
MSYS2). The zip holds the converted scenes, including the Stanford scans:
keep it to yourself.

`--games` also installs GTA and Screamer Rally from MGA-Glide's local game
fixtures into `C:\GAMES`, with launchers: `GTA` and `SR` run them on
MGA-Glide on the Matrox card, `GTA 3DFX` and `SR 3DFX` on 3dfx's runtime on
the Voodoo. That zip holds retail games: it is only for their owner's
machine.

## Loop A (86Box): correctness

```
python3 tools/run.py loopa --card g450                 # all tests, --quick
python3 tools/run.py loopa --card g450,g400,g200       # OpenGL frames compared across cards too
python3 tools/run.py loopa --card g450 --tests level
python3 tools/run.py compare out/loopa/g450 out/loopa/g200
```

One VM per card (the emulated Matrox card and an emulated Voodoo Graphics)
runs, in one job: BENCHGL (tag L), BENCHG on MGA-Glide's OVL (tag G) and, on
the first card, BENCHG on 3dfx's OVL on the Voodoo (tag V; `REF_OVL`,
default the GTA fixture in `~/.cache/mga-glide/fixtures/ovl/`). Then the
frame checks, with MGA-Glide's `tools/imgcmp.py` (tolerances in
`tools/checks.json`):

| Check | Gates |
|---|---|
| Glide on MGA-Glide against Glide on the Voodoo, same test | yes |
| OpenGL on each card against the first card | yes |
| Glide on each card against the first card | yes |
| OpenGL against Glide | no (advisory: the APIs rasterise differently) |

`checks.json`'s `cards` section relaxes single tests on one card, for what
that card cannot draw the same way: on the G100, S1BLND (stipple blending)
is compared through a 4x4 box filter and L1LQ2P (a pass that multiplies by
the destination) is advisory.

Output: `out/loopa/<card>/` (`serial.log`, `files/RESULTS.TXT`, `*.png`,
`diff/`, `programs.json`, `checks.json`).

The game tests (`docs/tests.md`, group `game`) need DOS-GL's Quake builds
(`make quake` in DOS-GL) and the game fixtures (DOS-GL's
`tools/quake/fixtures.py`, and `tools/quake/q2record.sh q2bench1` for the
Quake 2 demo):

```
python3 tools/run.py games --card g450                  # every game test
python3 tools/run.py games --card g450,g400,g200 --tests Q1D1,Q2D1
```

One job per card and game (the game's data on D:), running its timedemos in
turn; then each frame against the first card, and the 8-bit and
multitexture variants against their plain runs. Output:
`out/games/<card>/<game>/`; `tools/report.py ingest` takes these
directories like Loop A's.

## The bench (Loop B): numbers

Bench PCs are provisioned as in MGA-Glide's `docs/bench.md`. Once per PC,
copy the scenes over, then queue jobs:

```
python3 tools/run.py bench --pc bench-g450 --install-data
python3 tools/run.py bench --pc bench-g450                       # all tests, 640x480
python3 tools/run.py bench --pc bench-g450 --modes 640x480,800x600 --submit lists
python3 tools/report.py ingest ~/MGA-Glide/out/bench/bench-g450/<job> --pc bench-g450
```

A job runs BENCHGL then BENCHG (on the job's `GLIDE2X.OVL`) and uploads
`C:\OUT`. The virtual bench PC is the dry run:

```
cd ~/MGA-Glide && MGA_DOCKER_NETWORK=host tools/dev python3 tools/bench/vpc.py start g450 &
python3 tools/run.py bench --pc vbench-g450 --install-data
python3 tools/run.py bench --pc vbench-g450 --tests basic,S1 --secs 1
cd ~/MGA-Glide && python3 tools/bench/vpc.py stop g450
```

## Reports

```
python3 tools/report.py table                       # text tables of results/*.jsonl
python3 tools/report.py html                        # out/report.html
python3 tools/report.py table --include-loopa       # with Loop A records (emulator timings)
```

`results/<pc>.jsonl` holds the bench records (committed); Loop A records
(`results/loopa-*.jsonl`) are not committed. The page compares every target
(PC, API, runtime or library) per test: tables, bars, runs over time, and
the derived state-change cost.
