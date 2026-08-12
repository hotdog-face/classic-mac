# Celeste Classic — Macintosh Classic port

Porting **Celeste Classic** (Maddy Thorson & Noel Berry's original PICO-8
Celeste, <https://www.lexaloffle.com/bbs/?tid=2145>) to the original
**Macintosh Classic** (68000 @ 7.8 MHz, 512×342 1-bit, System 6/7), built with
Retro68 in C.

**Rights note:** game logic is vendored from
[ccleste](https://github.com/lemon32767/ccleste) (lemon32767's hand
translation of the Lua cart to portable C; no license file upstream), and the
game itself is Maddy Thorson & Noel Berry's. Private hobby port — ask both
before distributing, as was done for the Just One Boss port.

## Architecture: three reused layers + one thin new one

This port reuses the **Just One Boss** infrastructure nearly wholesale:

```
  celeste.c  (vendored ccleste — the ENTIRE game logic, unmodified*)
      |  14-op varargs callback (celeste.h interface)
      v
  celeste_glue.c  (new, ~130 lines — maps each op onto p8_*)
      v
  p8.c  — 128×128 index framebuffer PICO-8 emulation   [reused from JOB]
      v
  mac_display.c — 2× ordered-dither → 256×256 1-bit CopyBits  [reused]
  mac_input.c   — GetKeys → 6 buttons                          [reused]
  mac_main.c    — Toolbox init + 30fps decoupled-render loop   [reused]
```

\* one-line addition: `#include <stddef.h>` in celeste.h.

Data pipeline (`tools/gen_data.py`): ccleste's `gfx.bmp` (128×64 4bpp) →
`gfx_data.c` palette indices; `tilemap.h` → `map_data.c` (**MAP_H=64** —
Celeste uses the gfx-shared map rows, unlike JOB's 32) + `sprite_flags.c`.
Flags arrays in tilemap.h use C octal literals — the parser handles that.

Glue semantics copied from ccleste's `sdl12main.c` reference frontend,
including the `map()` mask special case (`mask==4 && flags==4`). Camera is
handled inside p8.c (draw calls pass pre-camera coords through).

## Status

- ✅ **Playable**: title screen + gameplay host-verified
  ([docs/host-title.png](docs/host-title.png),
  [docs/host-gameplay.png](docs/host-gameplay.png));
  builds to `Celeste.dsk` with Retro68.
- ✅ **Perf round 1** (after "unplayably slow" feedback):
  - game logic now builds in ccleste's `CELESTE_P8_FIXEDP` 16.16 fixed-point
    mode — no software float in the whole update.  celeste.c compiles as C++
    (operator overloading), no C++ runtime; two tiny local patches marked
    `local patch` / `local addition` in the source.
  - dirty-ROW present: unchanged p8_screen rows are neither re-dithered nor
    CopyBits'd (mac_display.c keeps a prev-frame copy; updateEvt →
    mac_display_invalidate()).
  - fps readout top-left (mac_main.c draw_fps) for measuring on hardware.
- ✅ **Readability round 1** (after "hard to see" feedback): luminance dither
  replaced with per-material 8x8 patterns (src/dither_tables.h, shared with
  host_test).  Roles verified against an index dump of room 1: sky 0 solid
  black + clouds 12 sparse dots, terrain interiors 5/1 light textures so
  platforms read as light masses, white snow/spike edges carry silhouettes,
  Madeline = near-white face + striped hair.  Tune further from playtests.
- ✅ **Perf round 2 — the ROOM CACHE** (round 1 wasn't enough; celeste
  re-tiles three full 16x16 map layers every frame, which IS the frame cost
  on a 68000).  celeste_glue.c prerenders bg-fill+mask4 / mask2 / mask8 into
  three index buffers once per room and replays them via p8_blit_layer()
  (camera+clip-aware, sentinel transparency; p8.c).  The full-screen rectfill
  is deferred and consumed by the mask-4 composite.  Bypassed whenever pal()
  is active (title flash) — and **host_test A/B-verified byte-identical**
  with cache on vs off at 5 checkpoints (`host_test N out script 0|1`).
  Clouds are compiled out on Mac (`CELESTE_MAC_NO_CLOUDS` local patch in
  celeste.c): 17 rectfills/frame, near-invisible at 1-bit, and they'd defeat
  the cache.  Snow particles kept.
- ✅ **Readability round 2** (owner screenshot: light-dotted cloud/snowbank
  fields fought the terrain): bg snowbank colors 6/12 pushed near-black so
  the background is uniformly dark; playfield contrast now belongs to
  terrain + white edges.  fps readout moved top-RIGHT on a black box (it
  was overprinting celeste's own timer at (1,1) — the "bd:bb:11" garble).
  ⚠️ ice rooms also use color 12 — retune when the owner reaches them.
- ✅ **Perf round 3 + walls-only look** (owner measured **FPS 02**; asked for
  bg removal):
  - the build had been compiling at **-O0** (no CMAKE_BUILD_TYPE) — now
    Release / -O2 / NDEBUG.  Likely the single biggest fps factor.
  - static frame (fill + terrain) now composes into ONE buffer restored per
    frame by a single 16KB memcpy (p8_blit_layer fast path); mask-2 map call
    becomes a no-op; sparse mask-8 fg blits only its occupied row band.
  - ambient snow particles compiled out (`CELESTE_MAC_NO_PARTICLES` local
    patch) — they dirtied rows everywhere and defeated the dirty-row present.
    Death-burst particles kept.
  - `celeste_glue_no_bg` (default ON): mask-4 bg decor layer skipped
    entirely — walls-only visuals per playtest feedback.
  - known cache-on approximation: platforms/big-chest draw over (not under)
    solid terrain; invisible in practice (they travel through open air).
  - A/B re-verified byte-identical (cache on/off, no_bg 0/1, 5 checkpoints).
- ✅ **FPS 30 confirmed by owner playtest** (Mini vMac) after round 3.
- ✅ **Round 4 polish** (owner: player "muddy", wants WASD):
  - crisp line-art player: Madeline's colors (8 hair, 15 face, 3/11 jacket)
    now SOLID WHITE — no dither on the character.  Spent-dash hair (12)
    = 50% checker gray so dash state stays readable (also un-hides ice).
  - controls: WASD moves (arrows still work), **J = jump, K = dash**,
    Return = confirm.  Title prompt swapped from "x+c" to a centered
    "wasd move  j jump  k dash" (glue-level string swap, ccleste-3DS style).
- ✅ **Round 5**: title text was printed in PICO-8 gray 5 → checker mush.
  Glue PRINT now forces any non-black text color to white (3x5 glyphs can't
  survive dither); title art color 13 made solid white (logo + mountain =
  clean line art).
- ⬜ Feel-check controls + player readability (owner).  Then real Classic.
  SD-card storage does NOT affect frame rate (disk image loads into RAM).
- ✅ **Audio round 1 — SFX** (owner: "no sounds yet"): all 21 sfx ids the
  game plays, converted from ccleste's WAVs (GBA maxmod set) by
  tools/gen_sfx.py → generated/sfx_data.c (8-bit offset-binary mono @
  11025 Hz, ~325KB).  mac_sound.c plays them on ONE sampled Sound Manager
  channel (quiet+flush then bufferCmd — newest sound wins).  Retro68's
  Multiverse.h lacks `initMono` (defined locally = 0).  SIZE.r added:
  1MB preferred / 640K min partition (samples live in the data segment;
  the old default partition would no longer fit).  If Mini vMac is silent,
  check its sound is enabled.
- ✅ **Audio round 2 — MUSIC**: all 5 tracks (mus0/10/20/30/40), decoded
  from the oggs by tools/gen_music.py (ffmpeg) to 8-bit mono @ 5563 Hz
  (22254.5/4, clean divisor), full-length so loops are seamless (~1MB).
  Plays on a SECOND sampled channel; looping via main-loop queue top-up
  (mac_music_idle keeps one full pass queued ahead — no interrupt
  callbacks, no A5 setup).  glue music() -> mac_music_play (index/10 slot,
  -1 stops, fade ignored).  App is now ~1.5MB: SIZE.r raised to 2MB/1.75MB
  — **needs a >=2MB Mac**; for a 1MB Classic rebuild without music_data.c.
  The .dsk grew past floppy size (1.6MB) — fine for Mini vMac / SD-based
  drives (Floppy Emu, BlueSCSI).
- ✅ **Pause + best-run saves**:
  - Esc or P opens a real PAUSE MENU: continue / restart / quit (W/S or
    arrows + Enter; Esc again = continue).  Game clock frozen, music keeps
    looping.  "quit" finally gives a clean exit besides Cmd-Q; "restart"
    returns to the title with a fresh seed.  The Enter press that closes
    the menu is swallowed until released so it can't fire a jump.
  - summit completion fires a `CELESTE_MAC_HOOKS` local patch in FLAG_draw →
    celeste_mac_completed(fruit, deaths, min, sec) → mac_prefs.c keeps best
    time / most berries / fewest deaths / total completions in a
    "Celeste Prefs" file (classic Create/FSOpen/FSWrite — System 6-safe,
    written next to the app).  The title screen shows
    "best MM:SS  N berries" once a run is on record.
  - ⚠️ prefs live on the game volume: rebuilding/replacing Celeste.dsk wipes
    them; run from a writable disk (or copy the app to the boot HD image)
    for records to persist.
- ✅ **Round 10** (owner: no File>Quit, crackling sound, wants real font):
  - MENU BAR added: File > Pause (cmd-P) / Restart / Quit (cmd-Q), mouse +
    command-key driven (MenuKey in keyDown; MenuSelect on inMenuBar clicks).
    Raw P/Esc still open the in-game pause menu; cmd modifier suppresses the
    raw-P path so cmd-P doesn't double-toggle.  No Apple menu (no DA support)
    — deliberate.
  - CRACKLING diagNosis: sfx + music both generated at full scale, and the
    Sound Manager ADDS channels → 8-bit overflow clipping.  Both generators
    now emit at -6dB (gen_sfx s/2, gen_music volume=0.5).  If crackle
    persists it's 2-channel mix CPU on the emulated Plus → fallback plan:
    duck music during sfx (single channel).
  - REAL PICO-8 FONT: tools/gen_font.py extracts the authentic glyphs from
    ccleste's font.bmp into p8_font.h (same 3x5/low-3-bits format).  PICO-8
    quirk kept: a..z cells are the big glyphs, A..Z the puny ones.
- ✅ **Round 11 — SINGLE-CHANNEL AUDIO** (owner confirmed crackle + dead
  controls appear TOGETHER; -6dB didn't help → the 2-channel mix itself was
  the pathology on old Sound Manager).  mac_sound.c rewritten to ONE sampled
  channel: music streams as ~0.5s chunks (queue kept >=1s ahead from the
  main loop, chunk ring of 8 SoundHeaders); an sfx flushes the channel,
  plays, and music chunks re-queue behind it — music DUCKS for the effect
  and resumes from a rewound position (flushed unplayed time subtracted).
  Audio regenerated at full scale (nothing sums on one channel).
- ✅ **Round 12 — the "level 2" bug was a 40-SECOND bug**: the streamer's
  per-chunk horizon math over-counted by +1 tick, so after ~40s (~= when
  you reach 200m) the code believed ~1s was queued while the channel was
  dry → permanent per-chunk stutter (crackle) + Sound Manager churn (input
  starvation).  Fixed with exact rational tick accounting (accumulator w/
  carry), a stall RESYNC in mac_music_idle (never binge-queue past a
  deficit — protects the 8-entry header ring), and sfx time excluded from
  the music rewind (gSfxEnd).  Rule: never pad audio-clock math.
- ✅ **Round 13 — SELF-CLOCKING streamer** (bug persisted even parked on
  level 1 → ANY tick-based queue estimate accumulates skew vs the Sound
  Manager's true consumption rate; the drift fix only removed one source).
  All clock math deleted: pacing now reads the channel itself --
  `SndChannel.qTail - qHead` (pending commands, the struct is public) +
  `SndChannelStatus.scChannelBusy`, topping the queue back up to a fixed
  depth of 3 buffers (~1.5s).  Music rewind on sfx = flushed pending chunk
  COUNT x chunk size (exact, no time conversion anywhere).  Nothing left
  that can drift.
- ✅ **Rounds 15-16 — the "audio bug" was a PHANTOM KEY all along.**
  Self-repro session (own Mini vMac instance, copy of the owner's System
  6.0.8 boot disk, AUDIO_DIAG overlay): music-only and sfx-stress soaks all
  healthy — queue-wrap theory dead, pacing exonerated.  Owner's failure
  frame showed the audio queue HEALTHY (P3/E0/FPS30) at the moment of
  "crackle + dead controls", with Madeline jumping uncommanded; owner
  confirmed the character "acts on her own".  A key gets latched in the
  emulated keymap (lost key-up); the game obeys the ghost key; each phantom
  jump restarts sfx = the crackle.  FIX: stuck-key auto-release guard in
  mac_input.c (movement masked after 10s continuous, jump/dash after 4s,
  until a genuine release).  Diag line: `p# c#### e# b## s####` — `b` shows
  raw pre-guard bits (stuck key visible), `s` = sfx count.  AUDIO_DIAG
  still on for validation; strip for the release build.
- ⬜ Real-hardware maiden voyage: needs >=2MB RAM + SD-based drive
  (Floppy Emu / BlueSCSI) since the .dsk is 1.6MB.  Screenshake toggle if
  ever wanted (ccleste has one).

## Build

- Mac target: `./build.sh` → `Celeste.dsk` (Retro68 at `~/Retro68-build`).
- Host test: see header of `tools/host_test.c`; scripted input
  `"frame:mask,..."`, mask bits 0..5 = L,R,U,D,jump,dash.

## Real-hardware perf plan (Celeste at 2-3fps on the actual Classic)

Owner measured on a 4MB Classic: sound ON = 2fps, sound OFF = 3fps.  So audio
is ~1/3 of the budget but NOT the wall; the **256x256 1-bit dither blit every
frame is the bottleneck** (Mini vMac ran faster-than-real and hid this).

The owner's own insight is the fix: the Donut screensaver is silky because it
does ZERO per-pixel work at runtime -- it just `CopyBits` prebuilt bitmaps.
Celeste can't fully prebake (it's interactive), but it can move in that
direction:

1. **Blit at 1x, not 2x.**  Today p8 renders 128x128 then scales+dithers to
   256x256 = 65536 output px/frame.  Rendering a 128x128 1-bit window (one
   dither pass, CopyBits 1:1) is 1/4 the pixels.  Biggest single win.
2. **Dirty-rect the dither.**  Already dirty-row on present; extend to only
   re-dither the sprite band, memcpy the static composite for the rest.
3. **Precompute a 2-px dither LUT.**  Map each pair of adjacent index bytes to
   an output byte via a 256-entry (or 65536 for pairs) table -- turns the
   inner dither loop into a table read.
4. **Audio: drop music rate or make sfx cheaper** (only ~1fps, do last).

Target: 1x blit alone should roughly double fps; LUT dither on top targets
playable (~8-12fps).  Ship an fps build so the owner can measure each step.

## Mouse tracking (owner: "gets wiped every update")

Mouse acceleration is a **PRAM value**, not stored on the disk -- so rewriting
HD20.dsk can't wipe it.  On the real Classic it resets when the **PRAM battery**
is weak/dead (a 3.6V 1/2-AA on the logic board; classic failure at this age).
Durable fixes, in order: (a) replace the PRAM battery -> the Mouse control
panel setting sticks forever; (b) set it once in Mini vMac (its PRAM file
persists across disk swaps); (c) a boot INIT that forces it -- deferred, since
a faulty INIT can stop the machine booting and mouse-feel can't be
screenshot-verified.
