#!/usr/bin/env python3
"""Convert a clip into a streamable movie.dat: a small header + every frame
dithered to 1-bit, concatenated.  The Mac 'Movie' app reads one frame at a
time off disk, so length is bounded by DISK space, not RAM.

.dat layout (big-endian):
  'MOV1'            4  magic
  frameCount        4  long
  width  height     2+2  short (pixels)
  fps               2  short
  reserved          2
  then frameCount * (height * width/8) bytes of 1-bit rows (1 = black).

Usage: gen_stream.py <clip> <out.dat> [fps] [size] [max_seconds]
"""
import sys, os, subprocess, tempfile, struct, numpy as np
from PIL import Image, ImageOps

def main():
    clip, out = sys.argv[1], sys.argv[2]
    fps  = int(sys.argv[3]) if len(sys.argv) > 3 else 15
    size = int(sys.argv[4]) if len(sys.argv) > 4 else 128
    maxs = float(sys.argv[5]) if len(sys.argv) > 5 else 600
    rowb = size // 8
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['ffmpeg','-v','quiet','-i',clip,'-t',str(maxs),'-vf',
            f'fps={fps},scale={size}:{size}:force_original_aspect_ratio=decrease,'
            f'pad={size}:{size}:(ow-iw)/2:(oh-ih)/2:color=black',
            f'{tmp}/f%05d.png'], check=True)
        files = sorted(f for f in os.listdir(tmp) if f.endswith('.png'))
        with open(out,'wb') as o:
            o.write(struct.pack('>4slHHHH', b'MOV1', len(files), size, size, fps, 0))
            for f in files:
                img = Image.open(f'{tmp}/{f}').convert('L')
                img = ImageOps.autocontrast(img, cutoff=1)   # dark footage
                img = img.convert('1')                        # FS dither
                black = (np.array(img, dtype=np.uint8) == 0).astype(np.uint8)
                o.write(np.packbits(black, axis=1).tobytes())
    n = len(files)
    mb = os.path.getsize(out) / 1024 / 1024
    print(f'{n} frames @ {fps}fps, {size}x{size}, {mb:.2f} MB, {n/fps:.1f}s')

if __name__ == '__main__': main()
