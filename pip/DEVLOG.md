# Pip - dev log

## Architecture chosen and why

**Background application with a small floating window** (the option we agreed on).

Under System 7 (or 6 + MultiFinder), an app that declares the right `SIZE`
resource flags keeps receiving event time in the background. Pip uses that to
keep a small draggable window parked in a screen corner and keep animating while
you work in another app. The `SIZE` resource (`pet.r`, flags `0x5C80`) sets
`canBackground`, `acceptSuspendResumeEvents`, `doesActivateOnFGSwitch`,
`backgroundAndForeground`, and `is32BitCompatible`.

Why not the alternatives:
- *Foreground-only app*: simplest, but the pet would freeze the moment you
  switched to another app, which defeats "shares the workspace."
- *Desk accessory*: coexists, but timing is constrained and DAs are an awkward,
  archaic build target with Retro68.
- *Faceless/backdrop app*: overkill for a corner companion and harder to interact
  with.

## Being a good citizen

- The event loop is `WaitNextEvent(everyEvent, &ev, 15, NULL)` (a ~0.25s sleep),
  so when nothing is happening Pip yields the CPU almost entirely.
- Animation advances on a timer (~7 fps) and only the tiny 76x84 offscreen is
  redrawn, and only when something actually changed (`gDirty`), then blitted with
  one `CopyBits`. No busy-waiting, no full-screen work.

## Behaviors and states

State machine: IDLE, ALERT, DROWSY, SLEEP, HAPPY, EAT.

- **Time of day** (`GetTime` -> hour): 10pm-6am -> SLEEP (Zzz, minimal motion);
  daytime -> awake.
- **Activity awareness:** a cursor-movement proxy (`GetMouse`, works in the
  background) plus our own events set `gLastActive`. Idle < ~1.5s -> ALERT (eyes
  follow your cursor, perky); idle > ~1 min -> DROWSY (half-lidded, naps).
- **Needs** (energy, playfulness, boredom, 0-100): drift about once a minute.
  Energy falls while awake and recovers while asleep; boredom rises when ignored;
  playfulness fades over time. They bias small behaviors (blink/look-around rate,
  whether it smiles).
- **Interaction:** click = pet (HAPPY: bounces, hearts, boredom resets, play up);
  `F` = feed (EAT: chews, then energy up and a happy bounce).

## Measured idle CPU

I could not run it in my environment, so this needs your eyes: with the window
parked and you working elsewhere, CPU cost should be negligible (it's asleep in
WaitNextEvent ~15 ticks at a time and only redraws a tiny bitmap a few times a
second). If the machine feels at all sluggish while Pip runs, raise the
WaitNextEvent sleep and/or the animation interval (both one-line changes) and
tell me what you saw.

## Cut or faked (for v1)

- **Persistence deferred.** Pip does not yet save mood/energy to a prefs file
  between launches. This was intentionally left for the next pass so the harder,
  test-sensitive parts (background app, animation, day/night, interaction) ship
  and run first, per the "alive first" plan. Next pass adds a small prefs file in
  the Preferences folder (FindFolder + FSpCreate/FSRead/FSWrite) with an
  elapsed-time catch-up so Pip comes back hungrier/more bored the longer it's been
  away.
- Needs are a light, legible drift, not a deep simulation.
- One creature, one art style; no sound.

## Still needs your eyes

- Confirm it compiles (including the `SIZE` resource via Rez).
- Confirm it runs in the background and keeps animating while another app is
  frontmost, that it visibly sleeps at night and perks up when you move the mouse,
  and that click-to-pet and F-to-feed react.
- Report rough idle CPU feel. Then I'll add persistence and we can raise the art.
