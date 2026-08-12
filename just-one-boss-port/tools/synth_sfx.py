#!/usr/bin/env python3
"""
synth_sfx.py -- offline PICO-8 SFX synthesizer for the Mac Classic port.

Real-time synthesis is impossible next to the game on an 8MHz 68000, so we
render each sound effect ONCE, here, to 8-bit unsigned mono PCM at the classic
Mac sample rate (22254.54/2 = 11127 Hz) and emit C arrays the Sound Manager can
play directly via bufferCmd (see src/mac_audio.c).

PICO-8 __sfx__ format (one 168-char hex line per sfx):
  bytes 0-3 : editor mode, speed, loop start, loop end   (2 hex chars each)
  then 32 notes x 5 hex chars: pitch(2) wave(1) vol(1) fx(1)

Synthesis model (documented PICO-8 behaviour, own implementation):
  note duration  = speed * 183 samples @ 22050Hz  (~speed/120.5 s)
  frequency      = 440 * 2^((pitch-33)/12)
  waves 0-7      = triangle, tilted saw, saw, square, 25% pulse, organ,
                   noise (pitched sample&hold), phaser (detuned tri pair)
  fx 0-7         = none, slide(from prev note), vibrato, drop, fade-in,
                   fade-out, arp fast, arp slow (arps approximated as plain)

Music: __music__ patterns are 4 channels of sfx indices (byte & 0x3f = sfx,
0x40 bit = channel off; flags 1/2 = loop start/end).  We mix each pattern's
channels and concatenate the chain into one PCM loop, rendered at HALF rate
(5563 Hz) to keep the loops small; mac_audio.c resumes them mid-loop after an
sfx interrupts.  Menu loop = patterns 37-42, boss loop = 11-18.

Usage: python3 synth_sfx.py source/just_one_boss.p8 src/generated [--wav DIR]
"""
import sys, os, math, random

OUT_RATE = 11127            # Mac 11kHz rate (22254.54566 / 2); music uses /2
MUSIC_RATE = 5564           # 22254.54566 / 4, matches Fixed 0x15BBA2E9
RATE = OUT_RATE             # active render rate (module-wide)
NOTE_UNIT = 183 / 22050.0   # seconds per speed unit
MAX_SECONDS = 4.0
# every sfx index the game actually calls (music tracks 32+ are a later phase)
USED = sorted({7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,28,29,30,31})

def parse_sfx(path):
    lines, cur = [], None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("__") and line.endswith("__"):
                cur = line.strip("_")
            elif cur == "sfx" and line.strip():
                lines.append(line.strip())
    out = []
    for row in lines:
        hdr = [int(row[i:i+2], 16) for i in range(0, 8, 2)]
        notes = []
        for i in range(8, min(len(row), 168), 5):
            chunk = row[i:i+5]
            notes.append({
                "pitch": int(chunk[0:2], 16),
                "wave":  int(chunk[2], 16),
                "vol":   int(chunk[3], 16),
                "fx":    int(chunk[4], 16),
            })
        out.append({"speed": max(1, hdr[1]), "loop": (hdr[2], hdr[3]), "notes": notes})
    return out

def freq_of(pitch):
    return 440.0 * 2.0 ** ((pitch - 33) / 12.0)

class Osc:
    """phase-continuous oscillator so note boundaries don't click"""
    def __init__(self):
        self.phase = 0.0
        self.noise_val = 0.0
        self.noise_next = 0.0
        self.noise_t = 1.0
    def sample(self, wave, freq):
        p = self.phase % 1.0
        if wave == 0:      # triangle
            v = 4*p - 1 if p < 0.5 else 3 - 4*p
        elif wave == 1:    # tilted saw
            v = (p / 0.875) * 2 - 1 if p < 0.875 else ((1 - p) / 0.125) * 2 - 1
        elif wave == 2:    # saw
            v = 2*p - 1
        elif wave == 3:    # square
            v = 1.0 if p < 0.5 else -1.0
        elif wave == 4:    # narrow pulse
            v = 1.0 if p < 0.3125 else -1.0
        elif wave == 5:    # organ: triangle + half-strength octave
            t1 = 4*p - 1 if p < 0.5 else 3 - 4*p
            p2 = (self.phase * 2.0) % 1.0
            t2 = 4*p2 - 1 if p2 < 0.5 else 3 - 4*p2
            v = 0.66 * t1 + 0.34 * t2
        elif wave == 6:    # pitched noise: smoothed sample & hold
            self.noise_t += freq * 6.0 / RATE
            if self.noise_t >= 1.0:
                self.noise_t %= 1.0
                self.noise_val = self.noise_next
                self.noise_next = random.uniform(-1, 1)
            v = self.noise_val + (self.noise_next - self.noise_val) * self.noise_t
        else:              # phaser: detuned triangle pair
            t1 = 4*p - 1 if p < 0.5 else 3 - 4*p
            p2 = (self.phase * 0.992) % 1.0
            t2 = 4*p2 - 1 if p2 < 0.5 else 3 - 4*p2
            v = 0.5 * (t1 + t2)
        self.phase += freq / RATE
        return v

def render(sfx):
    notes, speed = sfx["notes"], sfx["speed"]
    n_samp = max(1, int(round(speed * NOTE_UNIT * RATE)))
    # trim trailing silence
    last = -1
    for i, n in enumerate(notes):
        if n["vol"] > 0:
            last = i
    if last < 0:
        return b""
    osc, out = Osc(), []
    prev_pitch, prev_vol = None, 0
    ramp = max(1, int(0.0015 * RATE))          # anti-click ramp
    for i in range(last + 1):
        n = notes[i]
        base_p, vol, fx, wave = n["pitch"], n["vol"] / 7.0, n["fx"], n["wave"]
        from_p = prev_pitch if (fx == 1 and prev_pitch is not None) else base_p
        from_v = prev_vol if fx == 1 else vol
        for s in range(n_samp):
            t = s / n_samp
            pitch = from_p + (base_p - from_p) * t if fx == 1 else base_p
            v = from_v + (vol - from_v) * t if fx == 1 else vol
            if fx == 2:                             # vibrato
                pitch = base_p + 0.25 * math.sin(2 * math.pi * 7.5 * (len(out)) / RATE)
            elif fx == 3:                           # drop
                pitch = base_p - 12.0 * t
            elif fx == 4:                           # fade in
                v = vol * t
            elif fx == 5:                           # fade out
                v = vol * (1 - t)
            if v <= 0:
                osc.phase += freq_of(pitch) / RATE   # keep phase moving
                out.append(0.0)
                continue
            samp = osc.sample(wave, freq_of(pitch)) * v
            # per-note edge ramps
            if s < ramp:            samp *= s / ramp
            if s >= n_samp - ramp:  samp *= (n_samp - 1 - s) / ramp
            out.append(samp)
        prev_pitch, prev_vol = base_p, vol
        if len(out) > MAX_SECONDS * RATE:
            break
    # gentle one-pole lowpass: at 11kHz the raw squares/saws alias harshly
    # ("crackle" on the Mac speaker); this rounds the edges off
    out = lowpass(out, 0.55)
    # to 8-bit unsigned
    return bytes(max(0, min(255, int(128 + 110 * s))) for s in out)

def lowpass(samples, alpha):
    y, out = 0.0, []
    for x in samples:
        y += (x - y) * alpha
        out.append(y)
    return out

def parse_music(path):
    """__music__ lines: 'FF CCCCCCCC' = flags + 4 channel bytes"""
    pats, cur = [], None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("__") and line.endswith("__"):
                cur = line.strip("_")
            elif cur == "music" and line.strip():
                flags, chans = line.strip().split()
                pats.append({
                    "flags": int(flags, 16),
                    "ch": [int(chans[i:i+2], 16) for i in range(0, 8, 2)],
                })
    return pats

def channel_samples(sfx, total):
    """render one channel as floats, exactly `total` samples; a looping sfx
    cycles its loop region to fill, a one-shot pads with silence"""
    notes, speed = sfx["notes"], sfx["speed"]
    loop_s, loop_e = sfx["loop"]
    has_loop = loop_e > loop_s
    n_samp = max(1, int(round(speed * NOTE_UNIT * RATE)))
    ramp = max(1, int(0.0015 * RATE))
    osc, out = Osc(), []
    prev_pitch, prev_vol = None, 0
    i = 0
    while len(out) < total:
        if i >= 32 or i >= len(notes):
            if has_loop:
                i = loop_s
                continue
            out.extend([0.0] * (total - len(out)))
            break
        n = notes[i]
        base_p, vol, fx, wave = n["pitch"], n["vol"] / 7.0, n["fx"], n["wave"]
        from_p = prev_pitch if (fx == 1 and prev_pitch is not None) else base_p
        from_v = prev_vol if fx == 1 else vol
        for s in range(n_samp):
            if len(out) >= total:
                break
            t = s / n_samp
            pitch = from_p + (base_p - from_p) * t if fx == 1 else base_p
            v = from_v + (vol - from_v) * t if fx == 1 else vol
            if fx == 2:
                pitch = base_p + 0.25 * math.sin(2 * math.pi * 7.5 * len(out) / RATE)
            elif fx == 3:
                pitch = base_p - 12.0 * t
            elif fx == 4:
                v = vol * t
            elif fx == 5:
                v = vol * (1 - t)
            if v <= 0:
                osc.phase += freq_of(pitch) / RATE
                out.append(0.0)
                continue
            samp = osc.sample(wave, freq_of(pitch)) * v
            if s < ramp:            samp *= s / ramp
            if s >= n_samp - ramp:  samp *= (n_samp - 1 - s) / ramp
            out.append(samp)
        prev_pitch, prev_vol = base_p, vol
        i += 1
        if has_loop and i >= loop_e:
            i = loop_s
    return out

def render_music_chain(patterns, sfx_table, chain):
    """mix each pattern's active channels, concatenate into one loop (bytes)"""
    out = []
    for pi in chain:
        pat = patterns[pi]
        active = [sfx_table[b & 0x3f] for b in pat["ch"] if not (b & 0x40)]
        if not active:
            continue
        # pattern length = longest one-shot channel (all 32 notes)
        total = max(int(round(s["speed"] * NOTE_UNIT * RATE * 32)) for s in active)
        mix = [0.0] * total
        for s in active:
            ch = channel_samples(s, total)
            for k in range(total):
                mix[k] += 0.45 * ch[k]
        # fade the pattern's edges (~5ms): a channel cut mid-note at the
        # pattern boundary otherwise clicks at every join and on loop wrap
        edge = min(28, total // 2)
        for k in range(edge):
            g = k / edge
            mix[k] *= g
            mix[total - 1 - k] *= g
        out.extend(max(-1.0, min(1.0, v)) for v in mix)
    out = lowpass(out, 0.6)
    # normalize the whole chain to ~90% full scale
    peak = max(0.001, max(abs(v) for v in out)) if out else 1.0
    g = 0.9 / peak
    return bytes(max(0, min(255, int(128 + 127 * g * v))) for v in out)

def write_wav(path, pcm, rate=OUT_RATE):
    import struct
    with open(path, "wb") as f:
        data = pcm
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8))
        f.write(b"data" + struct.pack("<I", len(data)) + data)

def main():
    global RATE
    cart, outdir = sys.argv[1], sys.argv[2]
    wavdir = None
    if "--wav" in sys.argv:
        wavdir = sys.argv[sys.argv.index("--wav") + 1]
        os.makedirs(wavdir, exist_ok=True)
    table = parse_sfx(cart)
    rendered, total = {}, 0
    for idx in USED:
        if idx >= len(table):
            continue
        pcm = render(table[idx])
        if len(pcm) < 8:
            continue
        rendered[idx] = pcm
        total += len(pcm)
        if wavdir:
            write_wav(os.path.join(wavdir, f"sfx{idx:02d}.wav"), pcm)
        print(f"  sfx {idx:2d}: {len(pcm):6d} bytes  ({len(pcm)/RATE:.2f}s)")
    print(f"total PCM: {total} bytes ({total/1024:.0f} KB)")

    hpath, cpath = os.path.join(outdir, "sfx_data.h"), os.path.join(outdir, "sfx_data.c")
    with open(hpath, "w") as f:
        f.write("/* auto-generated by tools/synth_sfx.py -- do not edit */\n")
        f.write("#ifndef SFX_DATA_H\n#define SFX_DATA_H\n")
        f.write("typedef struct { const unsigned char *data; unsigned long len; } SfxSample;\n")
        f.write("/* 8-bit unsigned mono PCM @ 11127 Hz; NULL data == not rendered */\n")
        f.write("extern const SfxSample sfx_table[64];\n#endif\n")
    with open(cpath, "w") as f:
        f.write('/* auto-generated by tools/synth_sfx.py -- do not edit */\n')
        f.write('#include "sfx_data.h"\n\n')
        for idx, pcm in sorted(rendered.items()):
            f.write(f"static const unsigned char sfx{idx:02d}[{len(pcm)}] = {{\n")
            for i in range(0, len(pcm), 24):
                f.write("    " + ",".join(str(b) for b in pcm[i:i+24]) + ",\n")
            f.write("};\n")
        f.write("\nconst SfxSample sfx_table[64] = {\n")
        for i in range(64):
            if i in rendered:
                f.write(f"    {{ sfx{i:02d}, {len(rendered[i])} }},\n")
            else:
                f.write("    { 0, 0 },\n")
        f.write("};\n")
    print(f"wrote {hpath}, {cpath}")

    # ---- music loops (menu = patterns 37-42, boss fight = 11-18) ----
    RATE = MUSIC_RATE
    patterns = parse_music(cart)
    loops = {
        "menu":     render_music_chain(patterns, table, range(37, 43)),
        "boss":     render_music_chain(patterns, table, range(11, 19)),
        # one-shot stingers (played once by mac_audio, not looped)
        "gameover": render_music_chain(patterns, table, range(35, 37)),
        "victory":  render_music_chain(patterns, table, range(47, 49)),
    }
    for name, pcm in loops.items():
        print(f"  music {name}: {len(pcm)} bytes ({len(pcm)/MUSIC_RATE:.1f}s @ {MUSIC_RATE}Hz)")
        if wavdir:
            write_wav(os.path.join(wavdir, f"music_{name}.wav"), pcm, MUSIC_RATE)
    order = ["menu", "boss", "gameover", "victory"]
    mh, mc = os.path.join(outdir, "music_data.h"), os.path.join(outdir, "music_data.c")
    with open(mh, "w") as f:
        f.write("/* auto-generated by tools/synth_sfx.py -- do not edit */\n")
        f.write("#ifndef MUSIC_DATA_H\n#define MUSIC_DATA_H\n")
        f.write("typedef struct { const unsigned char *data; unsigned long len; } MusicLoop;\n")
        f.write("/* 8-bit unsigned mono PCM @ 5564 Hz.\n")
        f.write(" * 0 = menu loop, 1 = boss loop, 2 = game-over stinger, 3 = victory stinger */\n")
        f.write(f"#define MUSIC_COUNT {len(order)}\n")
        f.write("extern const MusicLoop music_table[MUSIC_COUNT];\n#endif\n")
    with open(mc, "w") as f:
        f.write('/* auto-generated by tools/synth_sfx.py -- do not edit */\n')
        f.write('#include "music_data.h"\n\n')
        for name in order:
            pcm = loops[name]
            f.write(f"static const unsigned char mus_{name}[{len(pcm)}] = {{\n")
            for i in range(0, len(pcm), 24):
                f.write("    " + ",".join(str(b) for b in pcm[i:i+24]) + ",\n")
            f.write("};\n")
        f.write(f"\nconst MusicLoop music_table[MUSIC_COUNT] = {{\n")
        for name in order:
            f.write(f"    {{ mus_{name}, {len(loops[name])} }},\n")
        f.write("};\n")
    print(f"wrote {mh}, {mc}")

if __name__ == "__main__":
    main()
