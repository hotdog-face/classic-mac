#!/usr/bin/env python3
"""GIF/clip -> full-screen (512x342) 1-bit baked frames for the demo player.
Source is scaled to fit 512x342 preserving aspect (letterboxed on black),
so it plays as large as possible without distortion."""
import sys, os, subprocess, tempfile, numpy as np
from PIL import Image
W, H, ROWB = 256, 256, 32   # 256 1:1 = smooth ~20fps on the real Classic; fullscreen 512x342 blits ran 5-8fps choppy
def main():
    clip=sys.argv[1]; maxf=int(sys.argv[2]) if len(sys.argv)>2 else 96
    fps=int(sys.argv[3]) if len(sys.argv)>3 else 20
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['ffmpeg','-v','quiet','-i',clip,'-vf',
            f'fps={fps},scale={W}:{H}:force_original_aspect_ratio=decrease,'
            f'pad={W}:{H}:(ow-iw)/2:(oh-ih)/2:color=black',
            f'{tmp}/f%04d.png'],check=True)
        files=sorted(f for f in os.listdir(tmp) if f.endswith('.png'))
        if len(files)>maxf:
            files=[files[i] for i in np.linspace(0,len(files)-1,maxf).astype(int)]
        frames=[]
        for f in files:
            img=Image.open(f'{tmp}/{f}').convert('L').convert('1')
            black=(np.array(img,dtype=np.uint8)==0).astype(np.uint8)
            frames.append(np.packbits(black,axis=1).astype(np.uint8))
    N=len(frames); out='src'
    with open(f'{out}/generated_frames.h','w') as h:
        h.write('#ifndef GEN_FRAMES_H\n#define GEN_FRAMES_H\n')
        h.write(f'#define DEMO_FRAMES {N}\n#define DEMO_W {W}\n#define DEMO_H {H}\n')
        h.write(f'extern const unsigned char demo_frames[{N}][{H}*{ROWB}];\n#endif\n')
    with open(f'{out}/generated_frames.c','w') as c:
        c.write('#include "generated_frames.h"\n')
        c.write(f'const unsigned char demo_frames[{N}][{H}*{ROWB}] = {{\n')
        for fr in frames: c.write('{'+','.join(str(b) for b in fr.flatten())+'},\n')
        c.write('};\n')
    print(f'{N} frames {W}x{H}, {N*H*ROWB//1024}KB')
if __name__=='__main__': main()
