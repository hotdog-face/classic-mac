# Pip - a desktop companion for the Classic Macintosh

Pip is a small original creature that lives in a corner of your screen and
shares the workspace with you. It breathes, blinks, looks around, follows your
cursor when you're active, gets drowsy and naps when you step away, is livelier
during the day, and curls up asleep at night. Click it to pet it; feed it and it
perks up.

It's built as a **background application**, so it keeps living while you work in
other apps, and it stays polite about CPU (long WaitNextEvent sleep, tiny
offscreen redraws only when something changes).

## Controls

- **Click Pip** to pet it (it bounces, hearts pop up)
- **F** to feed it (it eats, energy goes up)
- **Drag** its window to move it anywhere
- **Close box** or **Cmd-Q** to quit

## Build

The Retro68 toolchain is already installed from the HelloClaude build, so:

    cd ~/Downloads/Pip
    ./build.sh

That produces `Pip.dsk`. Boot a **System 7** disk in Mini vMac (background apps
need System 7 / MultiFinder), then drag `Pip.dsk` on and double-click Pip. Park
its window in a corner and switch to another app; Pip keeps living beside you.

## What it does

- **Time of day** from the Mac clock: livelier during the day, sleeps at night
  (roughly 10pm-6am), with Zzz's while it snoozes.
- **Activity awareness:** it watches your cursor and perks up when you're active,
  then gets drowsy and naps after you've been idle a while.
- **Needs:** energy, playfulness, and boredom drift slowly under the hood and
  nudge which little behaviors it plays (blinking, looking around, smiling).

## Notes / status

- This is v1, "alive first." Saving Pip's mood between launches (a small prefs
  file) is the planned next pass, deliberately deferred so the tricky parts
  (background app, animation, day/night) ship and run first. See DEVLOG.md.
- Written without a test-compile here. If the first build errors, paste it and
  I'll fix it. The `SIZE` resource in `pet.r` is what enables background running;
  if Rez complains about it, tell me and I'll adjust.
