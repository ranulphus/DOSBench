# Methodology

## What is compared

The same tests through Glide 2.x and OpenGL 1.1 on the same PC and card, and
Glide on Matrox (MGA-Glide) against Glide on a Voodoo (3dfx's runtime).
Each API is used the way games of the period used it:

- **Glide** (`src/backend/rb_glide.c`): the program transforms, clips and
  projects on the CPU and hands screen-space vertices to `grDrawTriangle`, as
  Glide games did. Each vertex a draw uses is transformed once; triangles
  wholly inside the guard band go straight to Glide, the rest are clipped in
  homogeneous space. Depth is a W-buffer; x and y are snapped to 1/16 pixel.
  Textures are downloaded into the TMU's 2 MB when first bound; when it is
  full, everything is evicted (a period texture cache).
- **OpenGL** (`src/backend/rb_gl.c`): object-space vertex arrays with the
  matrices loaded; DOS-GL transforms, clips and sets up. Static geometry goes
  through vertex arrays by default (`--submit lists` for display lists,
  `--submit immediate` for `glBegin`/`glEnd`); per-frame geometry (the
  level) always goes through arrays or immediate mode.

So Glide figures include the program's own transform cost and OpenGL
figures include DOS-GL's. The `submit_ms` figure separates the CPU time
spent submitting a frame from the time spent in the swap.

## Timing

`src/core/timer.c` uses the Pentium's time-stamp counter, calibrated at
start-up against the BIOS tick (1193182/65536 Hz): three windows of six ticks
(about 0.33 s each) aligned to tick edges, median taken. The counter
frequency is reported as `cpu_mhz`. Without a TSC (a 486) the BIOS tick is
used, at 55 ms resolution; DJGPP falls back to `uclock()`.

## Measurement rules

- Vsync off (`--vsync` to turn it on); the swap interval is 0 in Glide.
- Warm-up frames first (3; 1 in `--quick`), not measured; the hardware is
  idled before timing starts.
- Timed tests draw frames until at least 20 frames and their time have
  passed, at most 2000 frames: 3 s per feature-test phase and 5 s per model
  (the registry's `secs`), or `--secs` seconds for all of them. Rates are per
  second, so the time changes their noise, not their value. Camera paths
  (the level, the game scenes) always draw the same frames: every frame of
  the path.
- Between measurements the screen shows a title card before each suite and
  each test that is not a phase, a caption over a phase's first frame, and a
  summary after each suite (`--captions S`: held S seconds, cards twice as
  long; 0 for none). They are drawn before the warm-up and never in a timed
  frame or a saved shot, so they change no result. The text is the video
  BIOS's own 8x16 font, read at start-up.
- Every frame is timed from one swap's return to the next: frame time.
  The time from starting the frame to calling swap is the submit time.
- After measuring, the hardware is idled and the total time taken, so
  queued work is counted.
- Reported: average fps, average, median, 99th-percentile (the "1% low"),
  minimum and maximum frame times, and the test's rate: Mpixels/s for fill
  tests, Ktris/s for triangle tests, Mtexels/s for uploads, all from the
  per-frame work divided by the measured time.
- The frame saved for image checks (`--shots`) is drawn after measuring,
  at a fixed frame number, so it never affects the timings.

## Scaled and zoomed modes

The Matrox BIOSes offer 16-bit 640x480, 800x600, 1024x768 and 1280x1024
(1600x1200 only in the G100's). DOS-GL and MGA-Glide show the other sizes
in a larger BIOS mode: scaled, where the drawing engine copies each frame
into the display mode at every swap, or zoomed, where the chip repeats
lines and pixels at no cost. A scaled mode's frame times include that copy
(DOS-GL reports it separately as DGL-STAT `present_ms`), so it is not the
same measurement as a native mode of the same size: the H line's `display=`
and `fit=` say which it was, and the report prints them beside the mode.

## What 86Box numbers mean

Nothing about real hardware: 86Box runs the Matrox 3D engine and the Voodoo
in software, with its own timing. Loop A checks correctness (the programs
run, the frames match); performance numbers come from the bench (Loop B).
`tools/report.py` keeps Loop A records apart and leaves them out unless
asked.

## The score

The DOSBench score is 100 x the geometric mean of the game scenes' average
frame rates (the registry's `weight` > 0 tests; equal weights), per program
run and mode. There is no reference machine: a score is a frame rate on a
log scale, so twice the frame rate in every scene doubles it, and one slow
scene cannot dominate it the way an arithmetic mean of frame rates lets the
fastest scene dominate.

A score needs every scored scene from the same program run and mode. A
skipped or failed scene (or one left out of `--tests`) gives no score: the
run is `incomplete`, with the missing scenes and why. A run stopped with Esc
is `aborted`. A `--quick` run still gets a score, marked `quick`: its frame
rates come from a few frames and mean little. The feature tests, models and
real games are reported beside the score, never in it. `scorever` is bumped
whenever the scenes or the formula change; scores of different versions are
not compared.

## Output

`<out>\RESULTS.TXT` (default `C:\OUT`), appended, one line per record,
space-separated `key=value`:

- `H` lines, one per program run and mode: `run` (an id), `prog`, `ver`,
  `build`, `tag` (L OpenGL, G Glide, V Glide on 3dfx's runtime), `api`,
  `impl` (runtime or library version), `card`, `mode`, `vsync`, `submit`,
  `timer`, `cpu_mhz`, `tex_kb`, `quick`, `session` (`--session`: the menu
  gives every program of one run the same id; `-` without one).
- `T` lines, one per test: `run`, `prog`, `tag`, `api`, `mode`, `test`,
  `group`, `parent` (the suite, for its phases), `status` (ok, skip, fail),
  `frames`, `secs`, `fps`, `avg_ms`,
  `med_ms`, `p99_ms`, `min_ms`, `max_ms`, `submit_ms`, `tris_frame`, the
  rates (`ktris_s`, `mpix_s`, `mtexel_s`), `tex_kb_frame`, `crc` of the
  saved frame, and test notes (`changes=`, `copies=`, `why=`...).
- `S` lines, one per program run and mode that selected a scored scene,
  after its last test: `run`, `prog`, `tag`, `api`, `mode`, `scorever`,
  `status` (ok, quick, incomplete, aborted), `score` (only when ok or
  quick), `scenes=k/n` (scenes with a result, of those scored), each scene's
  fps as `ID=fps`, and `missing=ID:why,...`. `tools/report.py ingest`
  recomputes each score from the run's `T` lines and reports a difference;
  `tests/fixtures/score.txt` holds cases the C code and `tools/registry.py`
  must both compute the same.

The same lines go over COM1 as `HX-STAT bench ...` for the harness.
