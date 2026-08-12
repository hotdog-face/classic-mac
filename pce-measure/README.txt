PCE cycle-accurate Mac measurement setup
=========================================
PCE (pce-macplus, Hampa Hug) emulates a real Mac Plus/Classic with CYCLE-ACCURATE
timing + memory contention -- unlike Mini vMac, it reproduces true 8MHz fps.
The game's on-screen "FPS NN" counter reads the real Classic framerate.

RUN:   open PCE.app --args -c mac-classic.cfg -r -R      (Mac Classic model, 512K ROM)
  or:  open PCE.app --args -c mac-plus.cfg -r -R          (Mac Plus model; our System 6.0.8
                                                           is Plus-compatible, boots reliably)
Both models share the same 68000 @ 7.8336 MHz + 512x342 screen, so fps is equivalent.

SPEED:  cpu.speed = 1  -> true real-time (game plays at real Classic speed)
        cpu.speed = 0  -> runs fast in wall-clock, but fps counter is SPEED-INDEPENDENT
                          (the VIA timer is cycle-locked to the CPU) -> still the true fps.
        Use speed=0 for quick reads, speed=1 to watch real-time behavior.

DISPLAY: terminal scale=2, aspect 1:1 = clean 2x window.

BOOT DISK: our game .dsk files are raw HFS (no partition map) so they DON'T boot as
  SCSI; and PCE's boot ignores the boot-block shell name AND the Finder "Set Startup".
  WORKING AUTO-LAUNCH: build a 1.44MB bootable floppy (machfs, bootable=True) whose
  System Folder holds {System, <game>} where the GAME is given type 'FNDR' + named
  "Finder" -- PCE launches the type-FNDR shell = the game, with NO mouse needed.
  (See how job-boot.dsk was built in the session notes.)

INPUT: keyboard works reliably. Mouse coordinate mapping is unreliable -> avoid the
  Finder; use the FNDR-shell auto-launch above, then drive with the keyboard.
  JOB: press RIGHT to start; step on white magic tiles to progress the boss fight.

ROMs shipped with pce at: /opt/homebrew/.../share/pce/macplus/ (mac-classic.rom, mac-plus.rom)
