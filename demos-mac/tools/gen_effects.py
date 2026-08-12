#!/usr/bin/env python3
"""Pre-render looping demoscene effects to FULL-SCREEN (512x342) 1-bit Mac
BitMap frame arrays.  Runtime just CopyBits the whole frame to the screen
1:1 -- fills the display, no runtime scaling.  Round objects (donut, cube)
use a min-dimension projection so they stay round with natural side bars;
field effects (plasma, starfield, tunnel) fill the whole rectangle."""
import sys, os, numpy as np
from PIL import Image, ImageDraw

W, H = 512, 342
ROWB = W // 8
BAYER = (np.array([[0,32,8,40,2,34,10,42],[48,16,56,24,50,18,58,26],
                   [12,44,4,36,14,46,6,38],[60,28,52,20,62,30,54,22],
                   [3,35,11,43,1,33,9,41],[51,19,59,27,49,17,57,25],
                   [15,47,7,39,13,45,5,37],[63,31,55,23,61,29,53,21]])+0.5)/64.0
THR = np.tile(BAYER, (H//8+1, W//8+1))[:H,:W]
yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
cx, cy = W/2.0, H/2.0
S = min(W, H)                                    # scale basis for round objects

def dither(v): return v > THR

def donut(f, N):
    R1,R2,K2 = 1.0,2.0,5.0
    K1 = S*K2*3/(8*(R1+R2))
    th = np.linspace(0,2*np.pi,200,endpoint=False)
    ph = np.linspace(0,2*np.pi,500,endpoint=False)
    T,P = np.meshgrid(th,ph); ct,st=np.cos(T),np.sin(T); cp,sp=np.cos(P),np.sin(P)
    circx,circy = R2+R1*ct, R1*st
    A=4*np.pi*f/N; B=2*np.pi*f/N
    ca,sa,cb,sb=np.cos(A),np.sin(A),np.cos(B),np.sin(B)
    x=circx*(cb*cp+sa*sb*sp)-circy*ca*sb
    y=circx*(sb*cp-sa*cb*sp)+circy*ca*cb
    z=K2+ca*circx*sp+circy*sa; ooz=1.0/z
    xp=(cx+K1*ooz*x).astype(int); yp=(cy-K1*ooz*y).astype(int)
    L=(cp*ct*sb-ca*ct*sp-sa*st+cb*(ca*st-ct*sa*sp))/np.sqrt(2)
    lum=np.zeros((H,W)); zb=np.zeros((H,W))
    ok=(xp>=0)&(xp<W)&(yp>=0)&(yp<H)&(L>0)
    xf,yf,of_,lf=xp[ok],yp[ok],ooz[ok],np.clip(L[ok],0,1)
    order=np.argsort(of_)
    for xi,yi,oi,li in zip(xf[order],yf[order],of_[order],lf[order]):
        if oi>zb[yi,xi]: zb[yi,xi]=oi; lum[yi,xi]=0.13+0.87*li
    return lum>THR

def starfield(f, N):
    rng=np.random.RandomState(1234)
    sx=rng.uniform(-1,1,300); sy=rng.uniform(-1,1,300); z0=rng.uniform(0,1,300)
    img=Image.new('1',(W,H),0); d=ImageDraw.Draw(img)
    for i in range(300):
        z=(z0[i]+f/N)%1.0; zp=0.05+z*0.95
        px=cx+sx[i]/zp*cx; py=cy+sy[i]/zp*cy
        zpp=0.05+((z+1.0/N)%1.0)*0.95
        qx=cx+sx[i]/zpp*cx; qy=cy+sy[i]/zpp*cy
        if 0<=px<W and 0<=py<H:
            d.line([px,py,qx,qy], fill=1, width=1 if z<0.6 else 2)
    return np.array(img,dtype=bool)

def plasma(f, N):
    t=2*np.pi*f/N
    v=(np.sin(xx/26+t)+np.sin(yy/20)+np.sin((xx+yy)/30+t)+
       np.sin(np.sqrt((xx-cx)**2+(yy-cy)**2)/22-t))
    return dither((v+4)/8.0)

def cube(f, N):
    a=2*np.pi*f/N; b=4*np.pi*f/N
    V=np.array([[x,y,z] for x in(-1,1) for y in(-1,1) for z in(-1,1)],float)
    ca,sa,cb,sb=np.cos(a),np.sin(a),np.cos(b),np.sin(b)
    Rx=np.array([[1,0,0],[0,ca,-sa],[0,sa,ca]]); Ry=np.array([[cb,0,sb],[0,1,0],[-sb,0,cb]])
    Pp=V@Rx.T@Ry.T; pr=[]
    for p in Pp:
        zc=p[2]+4.0; s=S*1.15/zc; pr.append((cx+p[0]*s, cy+p[1]*s))
    E=[(0,1),(1,3),(3,2),(2,0),(4,5),(5,7),(7,6),(6,4),(0,4),(1,5),(2,6),(3,7)]
    img=Image.new('1',(W,H),0); d=ImageDraw.Draw(img)
    for i,j in E: d.line([pr[i],pr[j]], fill=1, width=2)
    return np.array(img,dtype=bool)

def tunnel(f, N):
    dx=xx-cx; dy=yy-cy; r=np.sqrt(dx*dx+dy*dy)+1e-3; ang=np.arctan2(dy,dx)
    u=ang/(2*np.pi)*8.0; v=(1.0/r)*40.0+f/N*2.0
    tex=(np.floor(u)+np.floor(v))%2
    bright=np.clip(r/(W*0.5),0,1)*(0.5+0.5*tex)
    return dither(bright)

EFFECTS={'donut':(donut,64),   # 96 fullscreen frames = 2.1MB data segment -> system error ID 2 (address error) on the real Classic; 64 = 1.4MB, same as the demos that work'starfield':(starfield,64),'plasma':(plasma,64),
         'cube':(cube,64),'tunnel':(tunnel,64)}

def build(name):
    fn,N=EFFECTS[name]; frames=[]
    for f in range(N):
        black=~fn(f,N)
        frames.append(np.packbits(black,axis=1).astype(np.uint8))
    out=f'{name}-mac/src'; os.makedirs(out,exist_ok=True)
    with open(f'{out}/generated_frames.h','w') as h:
        h.write('#ifndef GEN_FRAMES_H\n#define GEN_FRAMES_H\n')
        h.write(f'#define DEMO_FRAMES {N}\n#define DEMO_W {W}\n#define DEMO_H {H}\n')
        h.write(f'extern const unsigned char demo_frames[{N}][{H}*{ROWB}];\n#endif\n')
    with open(f'{out}/generated_frames.c','w') as c:
        c.write('#include "generated_frames.h"\n')
        c.write(f'const unsigned char demo_frames[{N}][{H}*{ROWB}] = {{\n')
        for fr in frames: c.write('{'+','.join(str(b) for b in fr.flatten())+'},\n')
        c.write('};\n')
    Image.fromarray((~np.unpackbits(frames[N//3],axis=1).astype(bool)[:, :W]*255).astype(np.uint8)).save(f'/tmp/{name}_fs.png')
    print(f'{name}: {N} frames {W}x{H}')

for n in sys.argv[1:] or EFFECTS: build(n)
