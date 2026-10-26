# Measurement records

`<pc>.jsonl`: one JSON object per test result from the bench, written by
`tools/report.py ingest` from a job's `RESULTS.TXT`. Each object merges the
program run's header (`prog`, `api`, `impl`, `card`, `cpu_mhz`, `mode`,
`submit`, ...) with the test's figures (`test`, `fps`, `avg_ms`, `med_ms`,
`p99_ms`, `ktris_s`, `mpix_s`, ...; docs/methodology.md), plus `pc`,
`source` and `when`. Only numbers are kept here, never content or frames.

Loop A records (`loopa-*.jsonl`) describe 86Box, not hardware, and are not
committed.

`bench-g400.jsonl` (2026-10-26): the first real card, bench-g400. That's an IBM PC 300PL (Pentium II at 398 MHz,
440BX) with an AGP G400 16 MB, under GLOS (agent mode) on MS-DOS 7.1, at 640x480. DOSBench d0572f4 with DOS-GL
6fd0969 and MGA-Glide ec03889.
- The Glide records come from a second run (`--idle 900`): the first run's runner took Glide's Arena scene, a
  fixed 1,000 frames at about 4 fps, for a hang.
- The DOS-GL profile (`PROF=1`, `DGL_STATS=2`) of G1ARNA, G3SPAC and M2BUNY gives about 9,000 cycles per drawn
  triangle in the library, with FIFO waits at about 6% of the time. Of that, about 3,300 cycles are vertex
  transform, projection and clipping, and about 5,200 are edge and gradient setup.
- The scenes are CPU-bound, not limited by the card or the bus. The fill tests (S1, about 160 Mpix/s) are the
  card's.
