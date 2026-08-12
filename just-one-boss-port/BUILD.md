# BUILD — handoff for the build agent

Goal: produce a **classic Mac application** (`JustOneBoss.APPL`) from this C source
using **Retro68**, and run it on a **Macintosh Classic** (or a 68k emulator).
Everything needed is in this repo; nothing depends on the original PICO-8 runtime.

This document is self-contained — a build agent should be able to follow it start
to finish without further context.

---

## 0. TL;DR

On this machine the Retro68 toolchain is already installed at `~/Retro68-build`
(shared with the `~/Downloads` Mac projects), so the whole thing is one command:

```bash
cd just-one-boss-port
./build.sh            # -> JustOneBoss.dsk (+ .bin) at the project root
```

**Status: this builds cleanly today** (verified: 13/13 ninja steps, 0 warnings,
produces an 800K `.dsk`). Then drag `JustOneBoss.dsk` onto a booted System disk
in Mini vMac (§4). On a machine without the toolchain, install Retro68 first (§2).

Under the hood `build.sh` runs cmake with the Retro68 toolchain file + Ninja; the
manual form is §3.

**Host sanity-check (no Retro68 needed):** §5 compiles the game logic with the
system `cc` and renders a frame to a PNG — the fastest way to iterate on gameplay.

---

## 1. What this is

A C port of the PICO-8 game *Just One Boss* to the original 1-bit black & white
Macintosh Classic. Architecture and rationale live in [PORTING-PLAN.md](PORTING-PLAN.md).
Two layers:

- **Portable** (no Mac headers): `p8.c` (PICO-8 API over a 128×128 index buffer),
  `engine.c` (entities/promises/movement), `game.c`, `entities.c`, `util.c`, and
  the generated cartridge data in `src/generated/`. Verifiable on any machine.
- **Mac glue** (classic Toolbox): `mac_main.c` (event loop + 30fps timing),
  `mac_display.c` (16-color→1-bit dither + `CopyBits`), `mac_input.c`
  (`GetKeys`→buttons). Only these touch `<Quickdraw.h>` etc.

Target: **System 6/7, 68000, no FPU, no Color QuickDraw.**

---

## 2. Installing Retro68

Retro68 is a GCC cross-compiler that builds classic-Mac binaries on
macOS/Linux. Reference: <https://github.com/autc04/Retro68>.

```bash
git clone https://github.com/autc04/Retro68.git
cd Retro68
git submodule update --init
# prerequisites (macOS): brew install cmake bison boost gmp mpfr libmpc
# prerequisites (Debian/Ubuntu): sudo apt install cmake bison texinfo \
#     libgmp-dev libmpfr-dev libmpc-dev libboost-all-dev
mkdir build && cd build
../build-toolchain.bash            # builds m68k (and PowerPC) toolchains; takes a while
```

After it finishes, the m68k toolchain + CMake toolchain file live under
`Retro68-build/toolchain/`. Export its path (used in §3):

```bash
export RETRO68=/path/to/Retro68/Retro68-build   # the dir containing toolchain/
ls $RETRO68/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake   # must exist
```

> If the exact path differs by Retro68 version, find the toolchain file with:
> `find /path/to/Retro68 -name retro68.toolchain.cmake`

---

## 3. Building the game

```bash
cd just-one-boss-port
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$RETRO68/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake ..
make -j
```

Produces in `build/`:
- **`JustOneBoss.APPL`** — the classic Mac application (resource+data forks split
  as `.APPL` + `.APPL.bin` depending on Retro68 version).
- **`JustOneBoss.bin`** — MacBinary (safe for transfer, preserves forks).
- **`JustOneBoss.dsk`** — a ready-to-mount 800K-ish disk image containing the app.

The build compiles all sources listed in [CMakeLists.txt](CMakeLists.txt); the
`add_application` macro (provided by the Retro68 toolchain file) handles the
resource fork / APPL packaging. No `.r` resource file is required — the app
creates its own window at runtime.

### If `add_application` is unknown
That means the toolchain file wasn't picked up. Re-check the
`-DCMAKE_TOOLCHAIN_FILE=...` path points at `retro68.toolchain.cmake` (§2).

---

## 4. Running it

**Emulator (recommended for iteration):**
- **Mini vMac** (models the actual Macintosh Classic, 1-bit) — closest to target.
  Mount `JustOneBoss.dsk`, or copy the app onto the emulated disk, launch it.
- **Basilisk II** (68k, System 7) also works; set monitor to **Black & White /
  1-bit** to match the target.

Getting the file in: mount the `.dsk`, or use the emulator's file-sharing, or
un-MacBinary `JustOneBoss.bin` with the emulator's import tool.

**Real Macintosh Classic:** transfer `JustOneBoss.bin` via a supported path
(floppy imaging with a suitable USB drive, a serial/AppleTalk link, or a
compact-flash SCSI setup), decode the MacBinary on the Mac, run the app.

**Controls:** Arrow keys move. **Z / , = 🅾️**, **X / . = ❎**. On the title,
press **→ (Right arrow)** to start, or **← (Left arrow)** for hard mode once a
win is saved. Quit via the **File menu (Quit, Cmd-Q)**; the Apple menu has an
About screen.

---

## 5. Host verification (do this first — catches 95% of issues without a Mac)

The portable half compiles and runs with the ordinary system compiler and
renders a dithered frame to a PGM/PNG, so you can confirm the game logic and the
exact 1-bit dither the Mac will use — no Retro68, no emulator.

```bash
cd just-one-boss-port
cc -std=c90 -Wall -Isrc -Isrc/generated \
   tools/host_test.c src/p8.c src/engine.c src/util.c src/entities.c src/game.c \
   src/generated/gfx_data.c src/generated/sprite_flags.c src/generated/map_data.c \
   -lm -o host_test

./host_test 30  title.pgm         # frame 30 -> the title screen
./host_test 185 stage.pgm play    # drives title->start->curtains->player walk
# view the .pgm directly, or convert:  (macOS) sips -s format png title.pgm --out title.png
```

Expected output matches the checked-in references
[docs/phase3-title.png](docs/phase3-title.png) and
[docs/phase3-stage.png](docs/phase3-stage.png). If the host frames look right,
any remaining problem is isolated to `mac_main.c` / `mac_display.c` /
`mac_input.c`.

---

## 6. Regenerating the cartridge data (only if the source cart changes)

`src/generated/*.c` are produced from the upstream cartridge and checked in.
To regenerate:

```bash
python3 tools/extract_cart.py source/just_one_boss.p8 src/generated
```

This re-emits the sprite sheet, sprite flags (entity metadata), and tile map as
C arrays, plus `audio_raw.txt` (raw SFX/music for the future audio phase).

---

## 7. Status & what the build agent should NOT be surprised by

**Working & host-verified:** data pipeline; the PICO-8 emulation layer; the
engine (entities/promises/bezier movement/hit detection/render sort); the 1-bit
dither; and the game up through the **tutorial slice** — title screen → press
Right → curtains open → move the player → collect tutorial magic tiles.

**Deliberately incomplete (not bugs):**
- **Audio is stubbed.** `sfx()`/`music()` are no-ops (`entities.c`, and no Sound
  Manager calls yet). The game is silent. Audio is a later phase; raw data is in
  `src/generated/audio_raw.txt`.
- **The boss and its attacks (Phase 4) are not ported.** After the tutorial bar
  fills, the boss-spawn milestones are stubbed (marked `TODO Phase 4` in
  `entities.c`/`game.c`). The game won't progress past the tutorial yet.
- **Font is a 3×5 stand-in**, not the exact PICO-8 glyphs (swap is data-only in
  `p8_font.h`).
- **The Mac glue (`mac_*.c`) now compiles and links** against Retro68 and the
  `.dsk` builds. Header nits already fixed for this toolchain: `<Windows.h>` (not
  `MacWindows.h`), no `KeyMap` typedef (use a raw `unsigned char km[16]` with
  `GetKeys`), and `libm` is linked for `floor/sin/cos`. What is *not* yet
  confirmed is on-device runtime behavior (window + `CopyBits` + event loop) —
  run the `.dsk` in Mini vMac to validate that last mile.

**Performance note:** positions use `float` (`typedef float num` in `engine.h`).
The 68000 has no FPU. If the frame rate is poor on real hardware, the plan is to
switch `num` to 16.16 fixed-point (localized to that typedef + `util.c` math) and
add a sin/cos LUT — see PORTING-PLAN.md §7. Don't do this preemptively; measure
first (the tutorial has few entities and should be fine).

---

## 8. File map (what to touch for what)

| Need to… | File |
|---|---|
| Change the main loop / timing / window | `src/mac_main.c` |
| Tune the B&W dither / blit | `src/mac_display.c` (`kBlackness`, `kBayer8`) |
| Change key bindings | `src/mac_input.c` |
| Fix a drawing primitive | `src/p8.c` |
| Add/port an entity | `src/entities.c` (register in `register_entity_classes`) |
| Scene flow / globals | `src/game.c` |
| Build config / add a source file | `CMakeLists.txt` |
| Regenerate art/map/flag data | `tools/extract_cart.py` |
