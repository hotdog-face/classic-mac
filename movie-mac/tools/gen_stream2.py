#!/usr/bin/env python3
"""Encode a clip into a streamable MOV2: inter-frame delta video + interleaved
8-bit audio, so the Mac player streams tiny per-frame records off disk with
sound.  Borrows MacFlim's ideas: XOR/delta between frames (only changed bytes
travel and get drawn), temporally-stable Floyd-Steinberg dithering (previous-frame
hysteresis kills the "boiling" and shrinks the deltas), and audio locked one
chunk per frame so A/V stay in sync structurally.

MOV2 layout (big-endian):
  header 32 bytes:
    'MOV2'(4) frameCount(u32) w(u16) h(u16) fps(u16) flags(u16)
    audioRate(u32) audioPerFrame(u32) keyInterval(u16) reserved(6)
    flags bit0 = has audio
  then frameCount records:
    type(u8: 0=delta 1=keyframe)  videoLen(u32)  video[videoLen]  audio[audioPerFrame]
    keyframe video = w/8*h raw 1-bit bytes (1=black)
    delta   video = runs of  dstOff(u16) len(u16) bytes[len]  filling videoLen

Usage: gen_stream2.py <clip> <out.dat> [fps] [size] [audioRate] [maxSeconds]
"""
import sys, os, subprocess, tempfile, struct, numpy as np
from PIL import Image, ImageOps, ImageFilter

BAYER8 = np.array([
    [ 0,48,12,60, 3,51,15,63],
    [32,16,44,28,35,19,47,31],
    [ 8,56, 4,52,11,59, 7,55],
    [40,24,36,20,43,27,39,23],
    [ 2,50,14,62, 1,49,13,61],
    [34,18,46,30,33,17,45,29],
    [10,58, 6,54, 9,57, 5,53],
    [42,26,38,22,41,25,37,21]], dtype=np.float32)

HYST = 34.0            # temporal hysteresis: keep last frame's pixel unless clearly changed
KEYINT = 24            # a full keyframe every N frames (seek/robustness)
TSMOOTH = 0.55         # grayscale temporal blend (kills film-grain flicker -> stable dither)
BUDGET = 2600          # max delta video bytes/frame; smallest changes "leak" (MacFlim)

def fs_dither_stable(gray, prev_bin):
    """Floyd-Steinberg (looks far better than ordered on video) made temporally
    stable, MacFlim-style: the black/white threshold at each pixel is biased by
    the PREVIOUS frame's pixel there, so a stable pixel keeps its value unless
    the new value is clearly different -> no boiling, tiny deltas.  Serpentine
    scan.  Lists in the hot loop for speed (~10s/clip)."""
    h, w = gray.shape
    out = np.zeros((h, w), np.uint8)
    thr_lo = 128.0 - HYST      # previous pixel was black -> stay-black bias
    thr_hi = 128.0 + HYST      # previous pixel was white
    err = [0.0] * (w + 2)
    for y in range(h):
        nerr = [0.0] * (w + 2)
        grow = gray[y].tolist(); prow = prev_bin[y].tolist(); orow = [0]*w
        even = (y & 1) == 0
        xs = range(w) if even else range(w-1, -1, -1)
        for x in xs:
            old = grow[x] + err[x+1]
            thr = thr_lo if prow[x] else thr_hi
            if old < thr:
                orow[x] = 1; e = old            # black; target 0
            else:
                orow[x] = 0; e = old - 255.0     # white; target 255
            if even:
                err[x+2] += e*0.4375; nerr[x] += e*0.1875
                nerr[x+1] += e*0.3125; nerr[x+2] += e*0.0625
            else:
                err[x] += e*0.4375; nerr[x+2] += e*0.1875
                nerr[x+1] += e*0.3125; nerr[x] += e*0.0625
        out[y] = orow
        err = nerr
    return out

def find_runs(cur, prev):
    """changed byte spans between two packed 1-bit arrays -> list of (off,len)."""
    diff = np.nonzero(cur != prev)[0]
    if len(diff) == 0:
        return []
    runs = []; start = diff[0]; prevb = diff[0]
    for b in diff[1:]:
        if b - prevb <= 3: prevb = b               # merge small gaps (run header is 4B)
        else: runs.append((int(start), int(prevb-start+1))); start = b; prevb = b
    runs.append((int(start), int(prevb-start+1)))
    return runs

def encode_delta(cur, decoder_state):
    """Emit runs against what the DECODER currently shows, capped to BUDGET.
    Kept runs are written into decoder_state (so the next delta is exact, and
    dropped changes intentionally leak the previous frame).  Returns bytes."""
    runs = find_runs(cur, decoder_state)
    # spend the budget on the biggest changes first (dither-noise specks are tiny)
    runs.sort(key=lambda r: r[1], reverse=True)
    out = bytearray(); spent = 0
    for off, ln in runs:
        if spent + 4 + ln > BUDGET and out:        # over budget -> leak the rest
            break
        out += struct.pack('>HH', off, ln) + cur[off:off+ln].tobytes()
        decoder_state[off:off+ln] = cur[off:off+ln]
        spent += 4 + ln
    return bytes(out)

def main():
    clip, out = sys.argv[1], sys.argv[2]
    fps  = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    size = int(sys.argv[4]) if len(sys.argv) > 4 else 256
    arate= int(sys.argv[5]) if len(sys.argv) > 5 else 7920
    maxs = float(sys.argv[6]) if len(sys.argv) > 6 else 600
    rowb = size // 8
    fbytes = rowb * size
    aperf = arate // fps
    arate = aperf * fps                          # make it divide evenly

    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['ffmpeg','-v','quiet','-i',clip,'-t',str(maxs),'-vf',
            f'fps={fps},scale={size}:{size}:force_original_aspect_ratio=decrease,'
            f'pad={size}:{size}:(ow-iw)/2:(oh-ih)/2:color=black',
            f'{tmp}/f%05d.png'], check=True)
        # audio: 8-bit unsigned mono at arate
        subprocess.run(['ffmpeg','-v','quiet','-i',clip,'-t',str(maxs),
            '-ac','1','-ar',str(arate),'-f','u8', f'{tmp}/a.raw'], check=False)
        audio = b''
        if os.path.exists(f'{tmp}/a.raw'):
            audio = open(f'{tmp}/a.raw','rb').read()
        files = sorted(f for f in os.listdir(tmp) if f.endswith('.png'))
        n = len(files)
        has_audio = 1 if len(audio) > 0 else 0

        with open(out,'wb') as o:
            o.write(struct.pack('>4sIHHHHIIH6x', b'MOV2', n, size, size, fps,
                                has_audio, arate, aperf, KEYINT))
            dstate = np.zeros(fbytes, dtype=np.uint8)         # what the DECODER shows
            gray_acc = None                                   # temporal-smoothed gray
            for i, fn in enumerate(files):
                img = Image.open(f'{tmp}/{fn}').convert('L')
                img = ImageOps.autocontrast(img, cutoff=1)
                img = img.filter(ImageFilter.GaussianBlur(0.6))
                gray = np.array(img, dtype=np.float32)
                if gray_acc is None: gray_acc = gray
                else: gray_acc = TSMOOTH*gray + (1-TSMOOTH)*gray_acc   # kill grain flicker
                prev_bin = np.unpackbits(dstate.reshape(size,rowb), axis=1)  # decoder's pixels
                bin_ = fs_dither_stable(gray_acc, prev_bin)
                pack = np.packbits(bin_, axis=1).reshape(-1)
                if i % KEYINT == 0:
                    vid = pack.tobytes()
                    o.write(struct.pack('>BI', 1, len(vid))); o.write(vid)
                    dstate = pack.copy()                       # keyframe = decoder fully synced
                else:
                    vid = encode_delta(pack, dstate)           # updates dstate in place
                    o.write(struct.pack('>BI', 0, len(vid))); o.write(vid)
                if has_audio:
                    a0 = i*aperf; chunk = audio[a0:a0+aperf]
                    if len(chunk) < aperf: chunk = chunk + b'\x80'*(aperf-len(chunk))
                    o.write(chunk)

    total = os.path.getsize(out)
    print(f'{n} frames @ {fps}fps, {size}x{size}, audio {arate}Hz/{aperf}B, '
          f'{total/1024/1024:.2f} MB ({total/max(1,n/fps)/1024:.0f} KB/s)')

if __name__ == '__main__': main()
