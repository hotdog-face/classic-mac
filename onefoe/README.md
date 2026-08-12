# One Foe

An original single-boss bullet-hell duel for the Classic Macintosh, black & white.

You fly a small ship at the bottom of a 128x128 playfield (drawn at integer 2x,
so a crisp 256x256 window) and wear down one boss across four attack phases. Beat
all four and you win. It's a from-scratch game: the boss, its patterns, and the
engine are all original.

## Controls

- **Arrows** move
- **Z** fire
- **X** focus (move slowly for precise dodging)
- **Space** start / retry
- **Cmd-Q** quit

## Build

The Retro68 toolchain is already installed from the HelloClaude build, so:

    cd ~/Downloads/OneFoe
    ./build.sh

That produces `OneFoe.dsk`. Boot a System disk in Mini vMac, then drag
`OneFoe.dsk` on and double-click One Foe.

## Tuning

All the knobs are grouped at the top of `onefoe.c`:

- `LOGIC_HZ` - fixed game-logic rate (default 30). Gameplay speed is constant at
  any frame rate, so lowering this only changes difficulty, not smoothness.
- `ENEMY_MAX` - max simultaneous enemy bullets. Lower it if the fight gets choppy.
- `BOSS_HP` - how long the fight lasts.
- `PLAYER_LIVES` - starting lives.

The current frame rate is shown live in the top-right of the HUD.

## Notes

- Targets the B&W Mac Plus that Mini vMac emulates: offscreen `BitMap` +
  `CopyBits`, an integer sine table (no FPU / no libm), and fixed-point bullet
  motion.
- Written without a test-compile in my environment. If the first build reports an
  error, paste it and I'll fix it. See DEVLOG.md for what was adapted and what
  still needs your eyes (measured FPS and a completion run).
