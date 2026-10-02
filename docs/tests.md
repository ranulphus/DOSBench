# Tests

Every test runs through both APIs from one portable core (`src/core/`), with
the render backend (`src/backend/rb.h`) the only difference. IDs are at most
seven characters because saved frames are named `<tag><id>.PPM` (8.3).

The catalogue is the registry, `src/core/tests.json`: its order is the run
order, and both programs, the menu and the tools are built from it
(`tools/registry.py`). Related tests form a suite (Model gallery, Fill rate,
Triangle rate, Texture memory, State changes, Quake level); each keeps its own
ID and result line, marked `parent=<suite>`. `BENCHG --list` or
`BENCHGL --list` prints the registry. `--tests` takes IDs, three-character
prefixes (`S2M`), suites (`FILL`), groups (`basic`, `scene`, `model`, `synth`,
`level`), presets (`full`: scenes, models and feature tests; `scenes`;
`features`) or `all`, comma-separated; `--tests-from FILE` reads the list from
a file.

## Basic

| ID | Draws | Checks |
|---|---|---|
| B0TRI | clear, one Gouraud triangle, one bilinear textured quad | the whole path works: load, open, draw, read back, results |
| B1SCN | `GSTEST.DBS`, 100 frames: every game-scene feature in one small scene (a lit model with a distant level of detail, a banked track, an orbit, spinning and animated models, a decal, blended, additive and glowing quads, scrolling, warping, ramping and flickering surfaces, smoke, sparks and a shockwave, sky, fog, and a camera shot of each kind) | the scene runtime draws the same through both APIs and on every card |

## Game scenes (group `scene`, scored)

Short game-like 3D sequences, the bulk of a run and the only tests in the
DOSBench score (docs/methodology.md). Each is a version 2 `.DBS` file made by
a generator in `tools/` (procedural, permissively licensed or built from
LibreQuake) and drawn by the scene runtime (`src/core/gs.c`): every frame of
its story once, at 25 story frames per second, with camera shots cut between
them. Frame f is a pure function of f, so every card draws the same frames;
what differs is how long they take. `make scene-stats` shows what each asks of
a card per frame, from a host replay.

| ID | Scene | Per frame (host replay, 640x480) | Stresses |
|---|---|---|---|
| G1ARNA | Arena (`ARENA.DBS`, `tools/scene_arena.py`), 1000 frames: LibreQuake's e0m1 walked with a rocket launcher in hand; 30 rockets with smoke trails at soldiers and dogs (LibreQuake's MDLs, animated and lit on the CPU) that shoot back or run at the player and fall dead, exploding boxes, embers over the torches, Quake's two-layer scrolling sky, warped water and lava, two cut-aways watching fights | about 11k triangles (the world twice: two-pass lightmaps), 75 draw calls, 4 screens of fill | the game's CPU work (visibility, vertex animation and lighting), two passes, overdraw from smoke and the sky |
| G2RACE | Canyon race (`CANYON.DBS`, `tools/scene_canyon.py`), 1000 frames: six buggies with spinning wheels round a 2.4 km banked dirt loop in a sandstone canyon, natural arches over the road, boulders and cacti, dust behind every car, backfires, haze from 300 m; a helicopter view, chase, trackside, bumper, orbiting and under-the-arch shots | about 7k triangles, 4 screens of fill (up to 9, from dust) | geometry, fog, blended dust close to the camera |
| G3SPAC | Space battle (`SPACE.DBS`, `tools/scene_space.py`), 1000 frames: a station with a spinning ring, a cruiser crossing the field, 200 spinning asteroids at three levels of detail, two teams of nine fighters on banked loops, laser bolts, five fighters lost (fireball, sparks, debris, smoke, shockwave), a last explosion on the cruiser; stars, nebulae and a planet around the camera | about 12k triangles, 100 draw calls, 2.8 screens of fill | many objects, each with its own transform and CPU lighting; draw calls; additive particles |
| G4CITY | City at dusk (`CITY.DBS`, `tools/scene_city.py`), 900 frames: 14 x 14 blocks with towers downtown, a river and bridges; the sky darkens and stars come out while windows light up one by one (an additive window pass over every facade), street lamps come on, 300 cars with head and tail lights, a helicopter with a searchlight; an orbit over downtown, the helicopter, a street corner, a dashboard, round the tallest tower, along the river | about 9k triangles, 290 draw calls (420 over the whole city), 5.5 screens of fill | fill and overdraw, many small draws, additive lights |

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
| PBD1 | PrBoom-plus (DOS-GL's `tools/doom`: the fork on SDL3 and DOS-GL) Doom II `demo1` (MAP11) on the OpenGL renderer: compatibility mode, the sky as a screen quad; the owner's IWADs, 640x480, no sound; the game writes its own H/T lines (`-dosbench`) | the first card |
| PBD1S | the same demo on the software renderer (8-bit VESA mode: the CPU's work, GL idle) | the first card |
| PBD2 | PrBoom-plus The Ultimate Doom `demo4` (E4M2) on the OpenGL renderer | the first card |
| PBD2S | the same demo on the software renderer | the first card |

