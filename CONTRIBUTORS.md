# Contributors

model2recomp is a Sega Model 2 board written as C, and the people who know
that board best keep turning up. Thank you to everyone who has contributed
code, fixes, testing, or a hard-won debugging insight. This file is the
canonical record of who did what. The changelog tells the story change by
change, but credit lives here.

The hardware model itself is a port of MAME's Model 2 driver. Its authors are
credited in [NOTICE](NOTICE) and in the README's Credits section.

If you've contributed and aren't listed, or a line here is wrong, open a PR
against this file. We want every name right.

---

## Maintainer

### Ned Heller — [@sp00nznet](https://github.com/sp00nznet)
Project creator and maintainer. The i960 execution model and field-sync frame
boundary, the function dispatch table, the ports of MAME's geometry engine,
rasterizer, MB86233 coprocessor and System 24 tilemaps, the CRX board variants,
and the corpus sweep.

---

## Contributors

### rob — [@biggestsonicfan](https://github.com/biggestsonicfan)
Brought what they had already worked out building
[m2-hle2](https://github.com/biggestsonicfan/m2-hle2), their own high-level
Model 2 emulator, and checked each fix against MAME's driver. One PR took
*Virtua Fighter 2* from a blank first field to its attract mode, got *Daytona
USA* past the link screen, and fixed the smearing in *Virtua Cop*'s attract.
Landed in #1:

*Geometry*
- **Link type 3 dropped the anchor corner.** A polygon with link type 3 starts
  a new strip from the previous polygon's far corner, but the case fell through
  to the default and left a stale corner. Every polygon after one was built
  from the wrong vertex, which drew Virtua Cop's gate as one smeared slab over
  the gunman behind it, and sent green wedges across the screen.
- **The viewport window is inclusive**, as MAME's `rectangle(min, max)` is,
  and an empty window draws nothing instead of falling back to the whole
  screen.

*Bus*
- **System 24 mirrors.** Tile RAM repeats at `+0x10000` and `+0x100000`, and
  char RAM and the sync registers at `+0x100000`. Writes through the mirrors
  were dropped, and with them the CRTC offsets that place the 3D picture.
- **Word stores to the 16-bit sync registers** now reach them.
- **Narrow stores to the TGP ports** were going through the read-modify-write
  the other regions use, which popped a pending reply off the output FIFO and
  pushed it back OR'd into the command. They now push the lane as a whole word,
  which Daytona needs because it sends some arguments with `stos`.
- **`0x06000000` is the upper half of the data ROM**, not the polygon ROM,
  which only the geometrizer can reach.

*TGP and i960*
- `lipl` loads the low 24 bits of P and keeps the top byte; the TGP ID port
  reads `TAH`/`AKO`/`ZAK`/`MTK` instead of a placeholder.
- **Idle timers read `0xFFFFF`, not 0.** VF2's random number generator mixes
  the four timer counts into its state. With all four stuck at 0 it returned
  one value forever, and a retry loop at `0x542C8` never exited, so the game
  never drew a field. Found from m2-hle2's VF2 notes and confirmed under gdb.
- **`chkbit` on a clear bit sets condition code 000, not LT**, which had
  inverted every `chkbit` + `bno`/`bo` pair; a rotate by 0 no longer shifts by
  32.

*Video*
- **Bit 2 of `0x0098000C` is the board's frame counter.** The loop XORed it
  into the stored value, so a game's own store moved the counter and 30 Hz
  games saw it flip at 60 Hz. It now comes from a separate field counter at
  the game's render rate.

*Corpus and portability*
- **The corpus builds and runs on Linux**: links libm, defaults single-config
  generators to Release, raises the stack limit to the 256 MB MSVC reserves,
  and reconfigures when a set was lifted after the last configure.
- **Daytona's operator backup RAM.** A factory-fresh battery sets the cabinet
  to link mode (LINK ID MASTER), so a single board waits for its partner
  forever. The sweep now writes the backup RAM an operator leaves on a single
  cabinet, with the checksum recomputed from the program ROM's own table, and
  the launcher loads it from `MODEL2_BACKUP`, so the board code stays
  title-agnostic.
- Brought the docs up to date: the copro data socket, the link-type table, the
  window edge rules, and the corpus rows for Daytona and VF2.

---

## A note on AI-assisted contributions

Parts of this project, and some contributions to it, were developed with the
help of AI coding tools. That's welcome here: what matters is that every change
is understood, reviewed, and verified by a human before it lands. If you used
an assistant, just say so in your PR (contributors here have) and make sure
you can stand behind the result.
