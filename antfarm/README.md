# AntFarm

A formicarium (ant farm) simulation for the Classic Macintosh, in black & white.

You watch a side-view cross-section of soil. A small colony starts in a central
nest with a shaft to the surface. Ants dig branching tunnels out of the dirt,
wander the galleries, and send foragers up to the surface to grab food crumbs
and haul them back to the nest. Every few crumbs stored, a new ant is born, so
the colony and its tunnel network slowly grow. Digging is capped so the soil
stays a network of galleries rather than an empty box.

## Controls

- **Click** on the surface to drop a food crumb there (the foragers come running)
- **Space** to pause / resume
- **R** to reset the colony
- **Cmd-Q** to quit

## Build

The Retro68 toolchain is already installed from the HelloClaude build, so:

    cd ~/Downloads/AntFarm
    ./build.sh

That produces `AntFarm.dsk`. Boot a System disk in Mini vMac, then drag
`AntFarm.dsk` on and double-click AntFarm.

## Notes

- Targets the black & white Mac Plus that Mini vMac emulates (offscreen bitmap
  + `CopyBits`, same approach as GrowCity).
- Written without a test-compile here, so if the first build reports an error,
  paste it and I'll fix it fast.
