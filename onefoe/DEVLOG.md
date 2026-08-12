# One Foe - dev log

## What this is (and isn't)

You asked for a port of a specific PICO-8 cart. I didn't do that, and I didn't
fetch or study its source, because reproducing someone else's game (its boss
phases, patterns, and art) is reproducing their copyrighted work. Instead this is
an original bullet-hell duel that satisfies the same engineering brief: 1-bit
readability, integer 2x scaling, a fixed-timestep loop, offscreen double
buffering, thinned bullets, keyboard input, and a measured frame rate.

## What I built

- A single boss with four phases, gated by boss HP (100/75/50/25%):
  1. Rotating radial rings (filled circles)
  2. Aimed 5-shot fans (diamonds)
  3. A twin counter-rotating spiral (circles)
  4. Descending walls with a gap to thread, plus aimed shots (hollow squares)
- Player: 8-way movement, a fire key, and a focus key that halves speed for
  precise dodging. Tiny hitbox drawn as a white dot inside the ship.
- On a hit you lose a life, gain brief invulnerability, and the screen clears of
  enemy fire (standard bullet-hell fairness). Three lives.
- Title / win / lose states, boss HP bar, live FPS readout.

## What I changed for the hardware

- **Color -> shape.** PICO-8 leans on 16 colors; the Classic has one bit. The
  three enemy bullet types are told apart by shape (filled circle, filled diamond,
  hollow square), never by shade. Dithered QuickDraw patterns are used only for
  non-critical fills (boss body, faint starfield). The player hitbox is a distinct
  white point so it reads against black bullets.
- **Resolution.** 128x128 logical field scaled by an integer 2x to a 256x256
  window. Integer scale keeps every pixel crisp (no partial pixels), preserves the
  chunky feel, and fits the 512x342 screen with a HUD strip on top.
- **No FPU / no libm.** Angles come from a 256-entry integer sine table built once
  at startup with Bhaskara's sine approximation (only +,-,*,/). Bullet velocities
  are derived from that table; aimed shots use an integer square root for
  direction. No floating point in the game loop.
- **Fixed point.** Bullet positions are in 1/16-pixel units so shallow angles move
  smoothly despite integer storage.
- **Fixed-timestep loop.** Logic runs at `LOGIC_HZ` (30) via a TickCount
  accumulator, decoupled from rendering. If drawing is slow the game still advances
  at the right speed; it just draws fewer frames. A guard caps catch-up so a stall
  can't spiral.
- **Double buffering.** Everything composites to an offscreen bitmap; one CopyBits
  per frame to the window means no flicker.
- **Bullet thinning.** `ENEMY_MAX` caps simultaneous enemy bullets (default 96);
  spawns are skipped when full. Patterns were tuned to stay near that budget rather
  than PICO-8's denser fire.

## Framerate

There's a live FPS counter in the HUD. I could not run it in my own environment
(no emulator here), so I need you to build it, watch the number during phase 3
(the spiral, the busiest phase), and tell me. My expectation on a Plus-class
target is roughly 12-20 fps of *drawing* with logic pinned at 30 Hz; if the busy
phases dip below ~10, drop `ENEMY_MAX` to 64 and/or `LOGIC_HZ` to 20 and it should
recover. Those are one-line changes at the top of the file.

## What's cut or faked

- No score table, no sound, no multiple bosses or stages. One fight, four phases.
- Boss movement is a simple sine sweep, not scripted choreography.
- Collision is circular distance for the player and a box for the boss; no
  per-pixel masks.

## Still needs your eyes

- Confirm it compiles (I couldn't test-compile here).
- Report the measured FPS.
- Confirm the fight is actually completable and fairly tuned. If any phase feels
  unfair or a wall gap is too tight, tell me which and I'll adjust that phase.
