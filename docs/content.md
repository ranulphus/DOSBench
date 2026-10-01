# Content: sources, licences and the scene format

Nothing here is committed: `tools/assets.py fetch` downloads each source,
checks its pinned sha256 and keeps it in `~/.cache/dosbench/dl`; `convert`
writes `build/data/*.DBS` and `build/data/CREDITS.TXT`.

| Scene | Source | Licence |
|---|---|---|
| TPOT4K, TPOT16K, TPOT64K | Utah teapot: Martin Newell's Bezier patches (1975), as published by the University of Utah Model Repository (`teapot_bezier`) | freely available; credit the University of Utah |
| BUNNY | Stanford Bunny, Stanford 3D Scanning Repository (`bunny.tar.gz`, `bun_zipper.ply`) | research use; credit the Stanford Computer Graphics Laboratory |
| DRAGON | Stanford Dragon, Stanford 3D Scanning Repository (`dragon_recon.tar.gz`, `dragon_vrip_res3.ply`) | research use; credit the Stanford Computer Graphics Laboratory |
| LANTERN | Lantern by sbtron (Microsoft), Khronos glTF Sample Assets | CC0-1.0 |
| SUZANNE | Suzanne by Norbert Nopper (UX3D), Khronos glTF Sample Assets | CC0-1.0 |
| AVOCADO | Avocado (Microsoft), Khronos glTF Sample Assets | CC0-1.0 |
| LQE0M1 | LibreQuake v0.09-beta `lite.zip`, `pak0.pak`: `maps/lq_e0m1.bsp` and `gfx/palette.lmp` | BSD-3-Clause (maps, textures; the release's `docs/COPYING`) |

The Khronos models are pinned to commit `7d4ba189` of
`KhronosGroup/glTF-Sample-Assets`. The Stanford scans are for research use:
they are fetched from Stanford by whoever builds the data and never
redistributed; a PC with the converted files is a copy made for that use.
The flame sprite texture and all test textures are procedural.

## Conversion

- **Teapot**: the 28 bicubic patches, tessellated uniformly to about 4k, 16k
  and 64k triangles; two-sided (the patches do not form a closed surface).
- **PLY**: ASCII or binary; faces fanned into triangles, degenerate ones
  dropped, winding made outward-facing (positive signed volume).
- **glTF**: node transforms applied; one batch per material; the
  base-colour PNG decoded (standard-library decoder in `tools/dbs.py`),
  reduced by 2x box filters and area resampling to at most 256x256.
- Models are centred and scaled to a bounding sphere of radius 10; lighting
  is baked into the vertex colours (ambient 0.3 plus diffuse 0.7 from a fixed
  direction).
- **LibreQuake** (`tools/bsp.py`): BSP29; the world model's faces fanned
  into triangles with texture coordinates from their texinfo; miptex
  converted through the palette (index 255 transparent in `{` fence
  textures; for sky, the back layer); lightmaps (style 0) packed into
  128x128 atlas pages with GLQuake's allocator, scaled by 1.8, and also
  sampled at each vertex for the baked mode; the BSP nodes, leaves, marks and
  PVS kept for visibility; the camera path routed through free space from
  the player start to the nearest unvisited items and monsters, then
  smoothed and resampled to 64-unit steps. Quake's z-up coordinates become
  y-up: (x, y, z) -> (x, z, -y).

## The .DBS format

Little-endian, a 16-byte header (`DBS1`, version 1, section count, 0) and
sections (4-character tag, 32-bit length, payload padded to 4 bytes):

| Tag | Content |
|---|---|
| TEXS | count; per texture: u16 width, height, flags (1 mipmap, 2 clamp, 4 lightmap), 0; RGBA8 texels |
| VERT | count; 32-byte vertices: f32 x, y, z; u8 r, g, b, a; f32 u, v; f32 u2, v2 |
| INDX | count; u16 indices, relative to their batch's first vertex |
| BTCH | count; 48-byte batches: u16 texture, lightmap (0xFFFF none), flags (1 sky, 2 translucent, 4 alpha-tested, 8 two-sided, 16 sprite), leaf; u32 first vertex, vertex count, first index, index count; f32 bounds |
| VIEW | u32 kind (0 orbit, 1 path), frames; f32 near, far; u32 fog colour; f32 fog start, end; u32 key count; then orbit centre, radius, height, or per key f32 position and look-at |
| VISL | levels: u32 counts; nodes (plane, children); leaves (contents, PVS offset, bounds, marks); marks; faces (batch, index count, first index); PVS bytes |
| INFO | text, `key=value` lines: name, what, credit, licence, conversion notes |

`src/core/scene.c` reads a file into one buffer and uses the sections in
place, after checking every count and range against the file size
(`tests/unit/test_scene.c` truncates a file at every 7th byte).

### Version 2: game scenes

A scene with a GHDR section is written as version 2; every other file stays
version 1, byte for byte. Version 2 adds batch flags 32 (additive), 64 (glow:
additive, never fogged) and 128 (decal), and these sections (fixed-size
records after a u32 count unless noted; field by field in `tools/dbs.py`'s
docstring and `src/core/scene.h`):

| Tag | Content |
|---|---|
| NRML | a table of up to 256 unit normals, then one u8 index per vertex (lit models) |
| GHDR | frames, story rate (25 frames/s), clear colour, fog, field of view, near and far planes, sun direction and colour, ambient colour, seed, sky model, the frame saved for image checks |
| MODL | models: a run of batches, flags (lit on the CPU, animated), the next level of detail and the distance it takes over, bounding sphere |
| VANM | vertex animations: per frame and vertex, x, y, z and a normal index in bytes, with a scale and origin (Quake MDL's packing) |
| TRAK | tracks: evenly spaced keys (position, roll), open or closed; Catmull-Rom between them |
| INST | instances: model, motion (static, along a track, spinning, orbiting), parent, the frames it exists, animation |
| PART | particle kinds: texture, additive or blended, flat or facing the camera, life, size, speed, spread, gravity, drag, rise, colours, spin |
| EMIT | emitters: a particle kind at a steady rate from a point on an instance (or in the world) |
| FXEV | bursts: a number of particles at one frame |
| CAMS | shots: from frame to frame, a camera on a track, chasing, fixed, mounted on an instance, or orbiting |
| SURF | batch effects: scrolling, warping (Quake water), lights switching on and off, pulsing or flickering |

Rules for game-scene content, so every card and runtime draws the same:

- Additive batches and particles blend ONE, ONE: alpha plays no part, so an
  additive texture carries its shape in its colour (black outside it).
  Blended ones (smoke, water, clouds) carry it in alpha.
- Particle and sprite textures are at least 32x32: an 8x8 texture magnified
  across a third of the screen shows where each runtime puts its bilinear
  texel centres (3dfx's and MGA-Glide's differ by half a texel).
- No multitexturing and no DST_COLOR passes outside the Arena, so the G100
  draws what the others draw (it stipples blends and skips multiplies).

`tools/dbs.py`'s `Scene.check()` refuses a game scene whose textures need
more than 1920 KB at 16 bits with mip chains (the 2 MB Glide TMU less room
for the overlay font), whose texture coordinates pass 32, whose references
point nowhere, or whose frames are not all covered by a shot. The runtime
(`src/core/gs.c`, `gsfx.c`) draws frame f as a pure function of f: nothing
carries from one frame to the next (particles are recomputed from their
emitter, number and birth time), so `make scene-stats` replays every frame
forward, reversed and shuffled on the host and the three must match.
