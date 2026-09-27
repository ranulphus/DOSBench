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
- Timed tests draw frames until at least 20 frames and `--secs` seconds (5
  by default) have passed, at most 2000 frames. Camera paths (the level)
  always draw the same frames: every frame of the path.
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

## What 86Box numbers mean

Nothing about real hardware: 86Box runs the Matrox 3D engine and the Voodoo
in software, with its own timing. Loop A checks correctness (the programs
run, the frames match); performance numbers come from the bench (Loop B).
`tools/report.py` keeps Loop A records apart and leaves them out unless
asked.

## Output

`<out>\RESULTS.TXT` (default `C:\OUT`), appended, one line per record,
space-separated `key=value`:

- `H` lines, one per program run and mode: `run` (an id), `prog`, `ver`,
  `build`, `tag` (L OpenGL, G Glide, V Glide on 3dfx's runtime), `api`,
  `impl` (runtime or library version), `card`, `mode`, `vsync`, `submit`,
  `timer`, `cpu_mhz`, `tex_kb`, `quick`.
- `T` lines, one per test: `run`, `prog`, `tag`, `api`, `mode`, `test`,
  `group`, `status` (ok, skip, fail), `frames`, `secs`, `fps`, `avg_ms`,
  `med_ms`, `p99_ms`, `min_ms`, `max_ms`, `submit_ms`, `tris_frame`, the
  rates (`ktris_s`, `mpix_s`, `mtexel_s`), `tex_kb_frame`, `crc` of the
  saved frame, and test notes (`changes=`, `copies=`, `why=`...).

The same lines go over COM1 as `HX-STAT bench ...` for the harness.
