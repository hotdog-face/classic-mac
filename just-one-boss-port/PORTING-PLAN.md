# Just One Boss — Macintosh Classic port

Porting Ayla Nonsense's PICO-8 game **Just One Boss**
(<https://github.com/aylanonsense/just-one-boss>) to the original **Macintosh
Classic** (1990: 68000 @ 7.8 MHz, 512×342 1-bit B&W, System 6/7), built with the
**Retro68** GCC cross-compiler in **C**.

Ported with the creator's permission.

---

## 1. The gap we're bridging

| | PICO-8 (source) | Macintosh Classic (target) |
|---|---|---|
| Resolution | 128×128 | 512×342 |
| Color | 16-color fixed palette | 1-bit black & white |
| CPU | (virtualized) | 68000 @ 7.8 MHz |
| Language | Lua (dynamic, GC, closures) | C (static, manual memory) |
| Frame rate | 30 fps | target 30 fps, degrade gracefully |
| Graphics API | `spr/sspr/rectfill/line/print/pal…` | QuickDraw `CopyBits` |
| Audio | procedural SFX + tracker music | Sound Manager (phase 5) |
| Input | 6 buttons | keyboard via `GetKeys` |
| Save | `cartdata` (4 slots) | a small Preferences file |

The two hard problems are **(a) 16 colors → 1 bit** and **(b) dynamic Lua →
static C**. Everything below is organized around isolating those two problems so
the ~2,400 lines of game logic can be ported almost mechanically.

---

## 2. Architecture: a thin PICO-8 emulation layer

The single most important decision: **we do not port PICO-8 semantics into each
entity.** Instead we reimplement the PICO-8 drawing API against our own
**128×128 byte framebuffer of palette indices** (`p8.c`). Every `spr`, `pal`,
`palt`, `rectfill`, `camera`, etc. behaves *exactly* as it does in PICO-8,
including the palette-swap tricks the game leans on heavily. Game code then reads
almost identically to the Lua original.

```
  game entities (engine.c / entities_*.c)
        |  call spr(), rectfill(), pal(), print()...
        v
  p8.c  ── logical 128x128 framebuffer (unsigned char, palette index 0..15)
        |  once per frame: convert to 1-bit
        v
  mac_display.c ── 128x128 indices ─►(2x scale + ordered dither)─► 256x256 1-bit
        |  CopyBits
        v
  Macintosh screen (256x256 window centered in 512x342)
```

### Why an index framebuffer (not draw straight to the screen)?
The game constantly calls `pal(a,b)` to remap colors mid-frame and `palt()` to
change transparency. Reproducing that against a 1-bit target directly is a
nightmare. Against a byte-per-pixel index buffer it's trivial and identical to
PICO-8. The color→B&W conversion happens **once per frame** at blit time, not per
draw call — which is also where it belongs for performance on a 68000.

---

## 3. Color → 1-bit strategy

PICO-8's 16 colors become 16 fixed **8×8 ordered-dither tiles** chosen by
luminance. At blit time each logical pixel (x,y) with color index `c` samples
`dither[c][ (y*2+dy) & 7 ][ (x*2+dx) & 7 ]` to pick black or white, for each of
the 4 output pixels of the 2× upscale. Because the dither is keyed to *screen*
coordinates, large flat areas render as stable, non-crawling patterns.

- Colors 0 (black) and 7 (white) map to solid. Everything else is a gray
  pattern from ~6% to ~94% black. The mapping table lives in `mac_display.c`
  (`kDitherLevel[16]`) and is tuned by eye against the real game.
- The playfield is 256×256, centered: screen origin (128, 43). The 43px top/
  bottom letterbox is used for nothing (matches the black PICO-8 border).

An alternate high-fidelity path (deferred): pre-render the *sprite sheet* to
1-bit once at boot using Floyd–Steinberg, and pick black/white per sprite pixel
from that. Ordered dither is the default because it composites correctly with
runtime `pal()` swaps; Floyd–Steinberg does not.

---

## 4. Dynamic Lua → static C

The Lua game is an entity-component system where entities are open tables with
arbitrary fields and closures. We collapse that into:

- **One `Entity` struct** (`engine.h`) holding the *superset* of every field any
  entity type uses (x, y, vx, vy, counters, per-type scratch in a small union/
  `var[]` bag). This is the standard, proven way to port PICO-8 ECS games to C.
- **A class registry**: `EntityClass classes[30]` with `draw`/`update`/`init`
  function pointers plus the flag metadata extracted from `__gff__`
  (see `src/generated/sprite_flags.*`). `extends` is resolved at spawn time
  exactly like the original `fget(sid+3)` inheritance.
- **The promise scheduler** (`make_promise`/`and_then*`) becomes a small pool of
  `Promise` structs with the same start/finish/cancel/chain semantics. No
  coroutines needed — the original is already a manual frame-counter scheduler.
- **Bézier movement** (`move`/`apply_velocity`) ports verbatim; it's plain math.
- **Closures** used as promise callbacks become named C functions referenced by
  an enum id (a tiny dispatch table), since we can't store closures.

### The 29 entity classes (from the real flag table)
```
 1 top_hat            11 player             21 particle
 2 spinning_top_hat   12 player_health      22 magic_mirror (boss)
 3 bunny              13 boss_health        23 magic_mirror_reflection (ext 22)
 4 curtains           14 magic_tile_spawn   24 (screen-ish, layer 5)
 5 screen (base)      15 (boss_gen, layer3) 25 bunny-ish (boss_gen)
 6 title_screen  (5)  16 player_reflection  26 (ttl150)
 7 credit_screen (5)  17 (boss_gen hitbox)  27 poof
 8 victory_screen(5)  18 (boss_gen layer4)  28 (ttl3, layer12)
 9 game_over_scr (5)  19 (boss_gen layer5)  29 (ttl32, layer10)
10 player_figment     20 (ttl7, hitbox1)
```
(Names in parens are `extends`.) Each gets ported one at a time; the scaffold
makes each port a self-contained `draw`+`update` function pair.

---

## 5. Repository layout

```
just-one-boss-port/
├── PORTING-PLAN.md          ← this file
├── Makefile                 ← Retro68 build
├── source/
│   └── just_one_boss.p8     ← upstream cartridge (reference of record)
├── tools/
│   └── extract_cart.py      ← .p8 → C data (run once; checked-in output)
└── src/
    ├── p8.h / p8.c          ← PICO-8 API emulation over an index framebuffer
    ├── p8_font.h            ← bitmap font for print()
    ├── mac_display.c/.h     ← index→1bit dither + CopyBits
    ├── mac_input.c/.h       ← GetKeys → 6 buttons
    ├── platform.h           ← timing / prefs / entry glue
    ├── mac_main.c           ← Toolbox init + 30fps main loop
    ├── engine.h / engine.c  ← Entity, spawn, update loop, promises, movement
    ├── entities.c           ← the 29 entity draw/update functions
    ├── game.c               ← _init/_update/_draw equivalents & globals
    └── generated/           ← auto-generated cartridge data (checked in)
        ├── gfx_data.*       ← 128×128 sprite sheet (palette indices)
        ├── sprite_flags.*   ← 256 entity-metadata bytes
        ├── map_data.*       ← 128×32 tile map
        └── audio_raw.txt    ← sfx/music, for the audio phase
```

---

## 6. Phased roadmap

- **Phase 0 — data pipeline.** ✅ `extract_cart.py` emits real C arrays for the
  sprite sheet, flags, and map.
- **Phase 1 — display + emulation layer.** ✅ `p8.c` framebuffer + primitives;
  `mac_display.c` dither blit; host-verified render ([docs/phase1-proof.png](docs/phase1-proof.png)).
- **Phase 2 — engine core.** ✅ Entity struct, class registry, spawn, the
  update/hitbox/render-sort loop, promises, bézier movement, `_init/_update/_draw`.
- **Phase 3 — entities, tier 1.** ✅ curtains, title screen, player (full step/
  teeter/bump state machine), player_health/figment, boss_health, magic tiles,
  particles, poof/pain/points. **Playable slice: title → press Right → curtains
  open → move → collect tutorial tiles**, host-verified
  ([docs/phase3-title.png](docs/phase3-title.png), [docs/phase3-stage.png](docs/phase3-stage.png)).
  Remaining tier-1 screens (credit/victory/game-over) come with Phase 4's flow.
- **Phase 4 — entities, tier 2 (the fight).**
  - **4a ✅** — the boss appears (mirror + animated face/hands/top-hat), the
    tutorial→boss milestones (spawn@25 / reveal@37 / intro@60), player damage +
    hearts, and a working **laser attack** driven by an explicit state machine
    (move to a column → charge preview → fire a column beam you must dodge).
    Host-verified ([docs/phase4-boss.png](docs/phase4-boss.png),
    [docs/phase4-laser.png](docs/phase4-laser.png)); still builds to `.dsk`.
  - **4b (next)** — the **action-sequence VM** (promise choreography with
    method+args), then the remaining attacks (cards, flowers, coins, thrown
    hats, bunnies), the boss hands' full pose/move choreography, all 5 phases +
    phase-change animations, reflections, and hard mode. Plus credit/victory/
    game_over screens. This is the bulk of the remaining game.
- **Phase 5 — audio.** Port the PICO-8 SFX synth to Sound Manager, or pre-render
  SFX to `snd ` resources offline. Music is a stretch goal.
- **Phase 6 — polish.** Prefs-file high scores, screen-shake, pause, perf tuning
  on real 68000 (fixed-point if `sin/cos`/float math is too slow).

---

## 7. Known risks / open questions

- **Float math on 68000.** The game uses `sin/cos/^` (bézier, easing, idle
  bob). The Classic has no FPU. If software float is too slow, convert hot paths
  to 16.16 fixed-point + a 256-entry sin LUT (PICO-8 angles are already turns).
- **Fill rate.** 256×256 dithered CopyBits every frame at 30fps is the perf
  cliff. Mitigations: dirty-rect blitting, or a hand-tuned index→1bit inner
  loop. Measure before optimizing.
- **`print()` fidelity.** Phase 1 ships a serviceable 3×5 font; swapping in the
  exact PICO-8 glyphs later is a data-only change to `p8_font.h`.
- **Button-prompt glyph.** The game draws the ❎/🅾️ button icon via `spr(190,…)`
  — that's sprite data, already covered by the sprite-sheet pipeline, not the
  font.
