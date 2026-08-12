# classic-mac

Software for the black-and-white compact Macintosh (Mac Plus / SE / Classic, System 6–7),
written in C and built with the [Retro68](https://github.com/autc04/Retro68) cross-compiler
on a modern Mac. Everything here runs on a stock 8 MHz 68000 with 1–4 MB of RAM — real
hardware or emulators (Mini vMac, Basilisk II, PCE).

Most projects include a prebuilt `.dsk` floppy image you can mount directly in an emulator
or copy over with a Floppy Emu / BlueSCSI. The disk images contain only our own software —
you supply your own System boot disk.

## Games

| Project | What it is |
|---|---|
| [onefoe](onefoe/) | **One Foe** — an original single-boss bullet-hell duel, four attack phases, crisp 2× 256×256 window. |
| [celeste-mac-port](celeste-mac-port/) | **Celeste Classic** running on a Mac Classic. Port of the PICO-8 original ([porting notes](celeste-mac-port/PORTING-NOTES.md)). |
| [just-one-boss-port](just-one-boss-port/) | Port of bridgs' PICO-8 gem **Just One Boss** ([build notes](just-one-boss-port/BUILD.md)). |
| [growcity](growcity/) | **GrowCity** — a tiny city-growing toy. |

## Toys & desk accessories

| Project | What it is |
|---|---|
| [pip](pip/) | **Pip** — a desk pet who lives on your Mac: yawns, scratches, gets startled, grumps, wanders, and speaks. |
| [antfarm](antfarm/) | **AntFarm** — an ant colony simulation in 1-bit. |
| [aquarium](aquarium/) | **Aquarium** — a desktop fish tank. |
| [dock-mac](dock-mac/) | A macOS-style dock for System 6/7. |

## After Dark modules

[after-dark-mac](after-dark-mac/) — native screensaver modules (`ADgm`) for Berkeley Systems'
**After Dark 2.0**, built with Retro68's `--mac-flat` output:

- **Ants** — the farm, after hours
- **Claude Show** — a three-act animated show
- **Cinema** — streams a film library to your screensaver

## Demos

[demos-mac](demos-mac/) — a little demoscene set pushing the 68000: **cube**, **plasma**,
**starfield**, **tunnel**, and the obligatory **spinning donut**.

[lum-mac](lum-mac/) — a frame-streamed animation piece (fan art of Lum from *Urusei
Yatsura*; character © Rumiko Takahashi).

[movie-mac](movie-mac/) — our early full-motion-video player experiment, with the
Python frame-stream generators. For real video on a compact Mac we now use Fred Stark's
excellent [MacFlim](https://github.com/fstark/macflim) — this stays here as the road we
took to get there.

## Tooling

[pce-measure](pce-measure/) — configs and notes for measuring *true* 8 MHz frame rates with
the cycle-accurate [PCE emulator](http://www.hampa.ch/pce/) (`brew install pce`), instead of
the too-fast timing most emulators give you. Bring your own ROM images.

## Building

Each project has a `build.sh` (CMake + the Retro68 toolchain). Install
[Retro68](https://github.com/autc04/Retro68), then:

```
cd <project> && ./build.sh
```

Output is a classic Mac application plus a mountable `.dsk` image.

## Credits & licensing

Original code here is MIT-licensed (see [LICENSE](LICENSE)).

The two ports are fan projects; the games belong to their creators:

- **Celeste Classic** © [Maddy Thorson & Noel Berry](https://www.lexaloffle.com/bbs/?tid=2145) —
  ported in the spirit of [ccleste](https://github.com/lemon32767/ccleste).
- **Just One Boss** © [bridgs](https://bridgs.itch.io/just-one-boss).

If you're a rights holder and want something changed or removed, open an issue.
