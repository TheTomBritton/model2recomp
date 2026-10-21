# Changelog

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Nothing is released yet. Virtua Cop boots, plays and draws against this
runtime, but the 3D output is visibly wrong, so there is no version worth
tagging. Everything below is unreleased.

## [Unreleased]

### Added

- `CONTRIBUTORS.md`, a Contributors section in the README, and a "Where your
  code comes from" section in `CONTRIBUTING.md`. (maintainer)
- CI on every push and pull request: build and run the tests on Windows
  (MSVC) and Linux (GCC); the tests are registered with CTest. (maintainer)
- The Model 2 memory map, interrupt controller, and the execution model the
  guest actually relies on: IAC reinitialisation, I/O acknowledgement, and the
  field-sync read the game busy-waits on. The frame boundary lives inside that
  wait loop, which is where presentation, input and interrupt dispatch happen.
- The System 24 tilemap layers, with the pair mask at `tile_ram` 0x6000/0x6800,
  window and split modes, and the real palette path — palram through 32
  `colorxlat` ramps per channel, indexed by luma, then gamma.
- The geometry engine and the Lockheed-Martin rasterizer, ported from MAME.
- The MB86233 "TGP" math coprocessor. Before it existed, the scene was flat,
  and that was misattributed to the recompiled floating point for two commits.
- Texture sampling: wrapping, bilinear filtering, mipmaps and the microtexture.
- Translucency in all three of the mechanisms the hardware has.
- Diagnostics: `MODEL2_SHADE` (how one polygon resolved its colour),
  `MODEL2_PROBE` (what drew one pixel), `MODEL2_MATRIX` (check the
  coprocessor's output), `MODEL2_RAMDUMP`, watchpoint call paths, culling
  counters, and a display-list command histogram.
- Host input plumbed through to the guest's DPRAM.

### Fixed

- The window tilemap is the other half of a pair, not a layer of its own;
  drawing it as a layer overdrew the screen.
- Tile pens are indices, not colours.
- Back-pass tilemap opacity now matches MAME, and unclipped window halves are
  skipped rather than drawn twice.
- The whole window rendered red: the texture format carries alpha in the red
  channel, and the SDL surface was `RGBX8888` where the data is `ABGR8888`.
- A quad fills its rectangle; it was filling half of it.
- The texture origin was off, and unpublished display lists were being parsed.
- Stale display lists were re-rendered after the guest had moved on.
- A guest frame leak that presented as an interrupt-frame problem.
- Every interrupt line is serviced, including the sound interrupt.
- Link type 3 now carries the anchor corner to the next polygon. It carried
  none, so Virtua Cop's gate smeared and a blue wedge crossed the screen. — *[@biggestsonicfan](https://github.com/biggestsonicfan)* (#1)
- The window's right and bottom edges are inclusive, and an empty window
  draws nothing instead of the whole screen. — *[@biggestsonicfan](https://github.com/biggestsonicfan)* (#1)
- The System 24 mirrors at `+0x100000`, and word stores to the sync
  registers, reach the hardware they mirror. — *[@biggestsonicfan](https://github.com/biggestsonicfan)* (#1)
- Byte and halfword stores to the coprocessor are pushed as whole words, as
  on the board, instead of being merged into a stale word first. — *[@biggestsonicfan](https://github.com/biggestsonicfan)* (#1)
- `lipl` keeps the top byte of P; `tgpid` returns the chip's ID string. — *[@biggestsonicfan](https://github.com/biggestsonicfan)* (#1)
- Idle timers read `0xFFFFF`, not 0, and `chkbit` sets the condition code to
  000 when the bit is clear. Virtua Fighter 2 reaches its attract mode. — *[@biggestsonicfan](https://github.com/biggestsonicfan)* (#1)
- `0x06000000` is the upper half of data ROM, not the polygon ROM. — *[@biggestsonicfan](https://github.com/biggestsonicfan)* (#1)
- Bit 2 of `0x0098000C` is the board's frame counter at the game's render
  rate; a store to the register no longer moves it. — *[@biggestsonicfan](https://github.com/biggestsonicfan)* (#1)
- The corpus builds and runs on Linux, and gives Daytona USA the
  single-cabinet backup RAM an operator leaves, so it gets past the link
  screen. — *[@biggestsonicfan](https://github.com/biggestsonicfan)* (#1)
- `model2recomp_test_tilemap` had failed 18 of its checks since the tilemap
  colour-path change of 2026-09-13: the test was stale, not the renderer.
  It now programs the colour-translate ramps, clears with layer 2 and
  scrolls by the negated origin. (maintainer)

### Known issues

Geometry renders but is wrong — walls as triangles, flat surfaces as slabs,
gaps in bodies. Virtua Cop's display list matches MAME's to 32,765 of 32,768
dwords, so the commands reaching this runtime are very nearly right and the
fault is downstream of the guest.
