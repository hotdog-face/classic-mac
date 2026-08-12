# Aquarium

An illustrated black & white fish tank for the Classic Macintosh.

This is the higher-fidelity version. Fish are drawn as shaded, dithered
illustrations, not flat silhouettes: rounded bodies with darker backs and lighter
bellies, outlined edges, gills, eyes, and animated tails and fins that undulate as
they swim. They steer and turn gradually instead of gliding, prefer their own
depth band, school loosely (minnows and tetras), and scatter when you tap the
glass. Depth is faked with three tiers drawn back-to-front, so near fish are
bigger and darker while far fish are smaller and hazier.

Behind the fish sits a pre-rendered background that stays put each frame: water
that darkens with depth, gravel, rocks, background plants, and a bubbling treasure
chest. Over the top drift light shafts, swaying kelp, rising bubbles, and a snail
patrolling the bottom.

## Controls

- **Click** in the tank to sprinkle food AND startle nearby fish (they dart away,
  then circle back to eat)
- **Space** to pause / resume
- **R** to reshuffle the tank
- **Cmd-Q** to quit

## Build

The Retro68 toolchain is already installed from the HelloClaude build, so:

    cd ~/Downloads/Aquarium
    ./build.sh

That produces `Aquarium.dsk`. Boot a System disk in Mini vMac, then drag
`Aquarium.dsk` on and double-click Aquarium.

## Performance & tuning

Illustrated fish cost more to draw than silhouettes, so if it feels slow:

- `MAXFISH` and the `addFish(...)` calls in `initTank()` set how many fish there
  are. Fewer fish = faster.
- The background is rendered once into its own buffer and copied each frame, so
  the per-frame cost is mostly the fish and the kelp/shafts.
- Species sizes live in the `BHL/BHH/TL5` tables and dither shades in `dpat[]`.

## Notes

- Targets the B&W Mac Plus that Mini vMac emulates: two offscreen bitmaps
  (static background + per-frame scene), `CopyBits` compositing, an integer sine
  table (no FPU), fixed-point motion and heading-based steering.
- I hand-designed the fish rather than tracing real photos (those aren't mine to
  use); the dithered shading is what gives them the illustrated, rounded look.
- Written without a test-compile here. If the first build reports an error, paste
  it and I'll fix it fast.
