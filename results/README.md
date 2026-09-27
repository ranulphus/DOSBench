# Measurement records

`<pc>.jsonl`: one JSON object per test result from the bench, written by
`tools/report.py ingest` from a job's `RESULTS.TXT`. Each object merges the
program run's header (`prog`, `api`, `impl`, `card`, `cpu_mhz`, `mode`,
`submit`, ...) with the test's figures (`test`, `fps`, `avg_ms`, `med_ms`,
`p99_ms`, `ktris_s`, `mpix_s`, ...; docs/methodology.md), plus `pc`,
`source` and `when`. Only numbers are kept here, never content or frames.

Loop A records (`loopa-*.jsonl`) describe 86Box, not hardware, and are not
committed.
