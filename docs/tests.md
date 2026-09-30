# Tests

Every test runs through both APIs from one portable core (`src/core/`), with
the render backend (`src/backend/rb.h`) the only difference. IDs are at most
seven characters because saved frames are named `<tag><id>.PPM` (8.3).

`BENCHG --list` or `BENCHGL --list` prints the table. `--tests` takes IDs,
three-character prefixes (`S2M`), groups (`basic`, `synth`, `model`,
`level`) or `all`, comma-separated; `--tests-from FILE` reads the list from a file.

## Basic

| ID | Draws | Checks |
|---|---|---|
| B0TRI | clear, one Gouraud triangle, one bilinear textured quad | the whole path works: load, open, draw, read back, results |

## Synthetic (group `synth`)

Geometry is laid out in pixels and drawn through the standard perspective
projection at a fixed eye distance, so it goes through the same transform
path as the scenes. Depth testing is off.

| ID | Draws | Reported |
|---|---|---|
| S1FLAT, S1GOUR | 8 full-screen layers (2 in `--quick`), flat or Gouraud | Mpixels/s |
| S1TEXP, S1TEXB | the same, textured 256x256, point-sampled or bilinear | Mpixels/s |
| S1BLND | textured bilinear, alpha-blended layers (the first is opaque) | Mpixels/s |
| S1FOG | textured bilinear, linear fog at 50% | Mpixels/s |
| S2L4, S2L16, S2L64 | 16384 Gouraud triangles of 4, 16 and 64 pixels, independent vertices | Ktris/s |
| S2M4, S2M16, S2M64 | the same as a shared-vertex grid mesh | Ktris/s |
| S2T16 | 16-pixel mesh, textured bilinear | Ktris/s |
| S2SA16, S2SL16, S2SI16 | 16-pixel mesh through GL vertex arrays, display lists, immediate mode (OpenGL only) | Ktris/s |
| S3UPL | a 256x256 texture replaced and drawn 4 times a frame | Mtexels/s uploaded |
| S3WS8, S3WS24 | 8 or 24 mipmapped 256x256 textures, one 64x64 quad each | fps, KB uploaded per frame |
| S3SUB | a 128x128 texture drawn as 16 quads, its 16 32x32 rectangles replaced first each frame (GLQuake's lightmaps; OpenGL only) | Mtexels/s uploaded |
| S3SUBI | the same, each rectangle replaced just before its quad is drawn (Quake 2 refilling its dynamic lightmap; OpenGL only) | Mtexels/s uploaded |
| S4D1, S4D16 | 8192 textured 16-pixel triangles, a draw call per 1 or 16 triangles | Ktris/s |
| S4T1, S4T16 | the same with a texture change per draw call | Ktris/s |
| S4B1, S4B16 | the same with a blend change per draw call | Ktris/s |

Notes:
- S2 triangles are right isosceles triangles, so "16 pixels" is the area.
- S3WS8 fits a 2 MB Glide TMU (8 x 171 KB); S3WS24 does not, so the Glide
  backend evicts and re-downloads every frame. DOS-GL keeps textures in the
  card's VRAM, so on a 16 or 32 MB card S3WS24 does not thrash there: that
  difference is what the test shows.
- The state-change cost (microseconds per change) is derived by
  `tools/report.py`: (S4Tk or S4Bk frame time - S4Dk frame time) / changes.

## Models (group `model`)

A camera orbits the model (360 frames a revolution), depth-tested, back faces
culled (the teapot is two-sided), textures modulated, bilinear and
mipmapped. Neither API path lights vertices, so `tools/assets.py` bakes a
fixed directional light into the vertex colours.

| ID | Scene | Triangles |
|---|---|---|
| M1TP4K, M1TP16, M1TP64 | Utah teapot, tessellated from the Bezier patches | 4.5k, 16k, 65k |
| M2BUNY | Stanford bunny (`bun_zipper.ply`) | 69k |
| M2DRGN | Stanford dragon (`dragon_vrip_res3.ply`) | 48k |
| M3LANT | Lantern (Khronos glTF sample, CC0), textured | 5.4k |
| M3SUZ | Suzanne (Khronos glTF sample, CC0), textured | 3.9k |
| M3AVOC | Avocado (Khronos glTF sample, CC0), textured | 682 |

## Level (group `level`)

A fly-through of LibreQuake's `lq_e0m1` on a fixed camera path (643 frames
at Quake's run speed; a timedemo: every run draws the same frames). Each
frame does what Quake's renderer did: find the camera's BSP leaf,
decompress its potentially visible set, keep the visible leaves inside the
frustum, and gather their faces' triangles per texture into index lists.
Then: sky, world textures, lightmaps, fences (alpha-tested), flame sprites
(alpha-tested camera-facing quads built each frame at the map's light
entities) and water (alpha-blended).

| ID | Lighting |
|---|---|
| L1LQ2P | two passes: textures, then lightmaps multiplied in with `DST_COLOR, ZERO` (GLQuake on one TMU) |
| L1LQBK | one pass: the lightmaps sampled at the vertices, as vertex colours |

The scene's textures fit a 2 MB TMU: world textures at 16 bits with mip
chains, lightmaps as 8-bit intensity (1.25 MB for e0m1 with nothing scaled
down; `tools/bsp.py` halves the largest textures when a map does not fit).

## Games (group `game`)

Timedemos of the Quake ports and Half-Life built on DOS-GL (DOS-GL's
`tools/quake`: the forks `qdos-dosgl` and `q2dos-dosgl`; `tools/halflife`:
Xash3D FWGS and hlsdk-portable), run by `tools/run.py games` from the
owner's game data (local fixtures, never committed). The games write their
own H and T lines (`-dosbench` in Quake, `td_dbtest` in Quake 2 and
Half-Life) and save frame 200 as `L<ID>.PPM`; game time advances a fixed
step per frame (`-fixedtime`, `fixedtime 14`, `host_framerate 0.02`, with
Half-Life's random numbers seeded by `td_seed`), so every run and every card
draws the same frames. The catalogue is `tools/games.json`.

| ID | Timedemo | Checked against |
|---|---|---|
| Q1D1 | GLQuake `demo1` (retail), two-pass lightmaps | the first card |
| Q1D1M | the same with multitexture lightmaps (`-mtex`) | Q1D1; skipped until DOS-GL has `GL_ARB_multitexture` |
| Q1D1P | the same with 8-bit paletted textures (`-8bit`) | Q1D1 |
| Q1LQ1 | GLQuake `demo1` on LibreQuake (free data) | the first card |
| Q2D1 | Quake 2 `q2bench1` (base1 walked and fired through; recorded by DOS-GL's `tools/quake/q2record.sh`) | the first card |
| Q2D1M | the same with multitexture lightmaps | Q2D1; skipped until DOS-GL has `GL_ARB_multitexture` |
| Q2D1P | the same with 8-bit paletted textures | Q2D1, looser: Quake 2 re-quantises its textures to the palette |
| HLD1 | Half-Life (WON) `hlbench1`: the c0a0 tram ride, world geometry (recorded by DOS-GL's `tools/halflife/run.sh record`); 128 MB PC, no sound | the first card |
| HLD2 | Half-Life (WON) `hlbench2`: c1a2, the map with the most monsters, MP5 fire | the first card |
| HLD1V | HLD1 on Xash3D's VBO path (`gl_vbo 1`): world lightmaps in the second texture unit, one pass (G400/G450: DOS-GL's buffer objects and combine) | HLD1 |
| HLD2V | HLD2 on the VBO path | HLD2 |
| FW1 | Fifth Wheel (`FIFTHWHEEL`: its game and the dgk kit on DOS-GL): 3,000 frames of the autopilot touring its generated 4 x 4 km world, one tick per frame, vsync off; no retail data, the files go on C: | the first card |
| FWP | Fifth Wheel's performance probe: one record per case, `FWP-<tris>-arr`/`-list`/`-imm` (1,000-8,000 triangles through vertex arrays, display lists, immediate mode), `-arr-tex` (textured), `FWP-2000-d<draws>` (50-400 draw calls), and an `FWP` summary; the game writes them itself (`dgk/bench.h`) | none: the records feed Fifth Wheel's budget model (its `docs/perf.md`) |

