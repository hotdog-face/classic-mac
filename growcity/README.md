# GrowCity

A growing isometric city simulation for the Classic Macintosh, in black & white.

A town seeds itself at the center, spreads roads outward, raises dithered 3D
buildings that get taller over time (tallest downtown), scatters a few parks,
and fills the streets with tiny people who wander between blocks. Everything is
drawn to an offscreen bitmap and blitted to the window, so the animation is
smooth and flicker-free even on a Mac Plus.

## Controls

- **Click** anywhere to drop a new road seed and start a district there
- **Space** to pause / resume
- **R** to reset the city
- **Cmd-Q** to quit

## Build

The Retro68 toolchain is already installed from the HelloClaude build, so:

    cd ~/Downloads/GrowCity
    ./build.sh

That produces `GrowCity.dsk`. Boot a System disk in Mini vMac, then drag
`GrowCity.dsk` on and double-click GrowCity.

## Notes

- Targets the black & white Mac Plus that Mini vMac emulates (basic QuickDraw,
  offscreen `BitMap` + `CopyBits`).
- It compiles cleanly against the Retro68 multiversal interfaces, but I wrote it
  without being able to test-compile here, so if the first build throws an error,
  paste the message and I'll fix it fast.
