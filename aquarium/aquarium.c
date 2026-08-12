/*
 * Aquarium - an illustrated black & white fish tank for the Classic Macintosh
 * ---------------------------------------------------------------------------
 * A higher-fidelity ambient aquarium. Fish are drawn as shaded, dithered
 * illustrations (rounded bodies with dark backs and light bellies, outlined
 * edges, animated tails and fins) rather than flat silhouettes. They steer and
 * turn gradually, prefer their own depth, school loosely, and scatter when you
 * click. Depth is faked with three tiers: near fish are bigger and darker, far
 * fish smaller and hazier. A static background (depth-graded water, gravel, a
 * bubbling treasure chest, background plants) is composited each frame with
 * drifting light shafts, swaying kelp and rising bubbles on top.
 *
 *   Click - drop food AND startle nearby fish (they scatter, then come to eat)
 *   Space - pause / resume
 *   R     - reshuffle the tank
 *   Cmd-Q - quit
 *
 * Two offscreen buffers (static background + per-frame scene), one blit to the
 * window per frame. Built with Retro68.
 */

#include <Quickdraw.h>
#include <MacTypes.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <ToolUtils.h>
#include <Memory.h>
#include <string.h>

#define WIN_W   480
#define WIN_H   300
#define WATER   8
#define SUBTOP  (WIN_H-28)
#define FP      16

#define MAXFISH 20
#define MAXBUB  28
#define MAXFOOD 40
#define NKELP   5

/* species: 0 minnow 1 tetra (schoolers) | 2 angel 3 puffer 4 catfish */
static const short BHL[5] = {12,  9, 11, 12, 18};   /* body half length */
static const short BHH[5] = { 5,  7, 14, 11,  6};   /* body half height */
static const short TL5[5] = { 8,  6,  7,  6,  9};   /* tail length      */
static const short CRU[5] = {20, 18, 12, 11, 14};   /* cruise speed x16 */

typedef struct {
    short x, y, head, spd, tgtY, wander, startle, frame, frameT;
    unsigned char sp, tier;
} Fish;
typedef struct { short x, y, vx; unsigned char on; short ph; } Bubble;
typedef struct { short x, y; unsigned char on; } Flake;

static Fish   fish[MAXFISH]; static short nfish = 0;
static Bubble bub[MAXBUB];
static Flake  food[MAXFOOD];
static short  kelpX[NKELP], kelpH[NKELP], kelpP[NKELP];
static short  snailX, snailDir, clickX, clickY;
static long   tk = 0;
static Boolean paused = false;

static GrafPort gBg, gOff;
static BitMap   gBgBits, gOffBits;
static Pattern  dpat[6];

static short SINT[256];
static unsigned long rs = 20240706UL;

static short rnd(short n) { rs = rs*1103515245UL + 12345UL; return (short)(((rs>>16)&0x7fff)%(n>0?n:1)); }
static short aabs(short a) { return a < 0 ? -a : a; }
static short sgn(short a) { return a>0 ? 1 : (a<0 ? -1 : 0); }
static void  setPat(Pattern *p, const unsigned char *b) { memcpy(p, b, 8); }

static void buildSin(void)
{
    short i;
    for (i = 0; i < 256; i++) {
        double deg = i * 360.0 / 256.0, s;
        if (deg <= 180.0) { double x = deg;         s =  4*x*(180-x)/(40500 - x*(180-x)); }
        else              { double x = deg - 180.0; s = -(4*x*(180-x)/(40500 - x*(180-x))); }
        SINT[i] = (short)(s * 256);
    }
}
static short sinT(short a) { return SINT[a & 255]; }
static short cosT(short a) { return SINT[(a + 64) & 255]; }

/* ---- buffers ----------------------------------------------------------- */

static Boolean makeBuf(GrafPort *port, BitMap *bits)
{
    Rect b; short rb; long sz;
    SetRect(&b, 0, 0, WIN_W, WIN_H);
    rb = ((WIN_W + 15) / 16) * 2;
    sz = (long)rb * WIN_H;
    bits->baseAddr = NewPtr(sz);
    if (bits->baseAddr == NULL) return false;
    bits->rowBytes = rb; bits->bounds = b;
    OpenPort(port); SetPortBits(bits);
    port->portRect = b; ClipRect(&b); RectRgn(port->visRgn, &b); EraseRect(&b);
    return true;
}
static void blit(WindowPtr w)
{
    Rect s = gOffBits.bounds;
    SetPort(w);
    CopyBits(&gOffBits, &w->portBits, &s, &w->portRect, srcCopy, NULL);
}

/* ---- setup ------------------------------------------------------------- */

static void addFish(short sp, short tier)
{
    Fish *f;
    if (nfish >= MAXFISH) return;
    f = &fish[nfish++];
    f->sp = sp; f->tier = tier;
    f->x = (20 + rnd(WIN_W-40)) * FP;
    if (sp == 4) f->tgtY = SUBTOP - 16;
    else if (sp == 2) f->tgtY = WIN_H/2 + rnd(30) - 15;
    else if (sp == 1) f->tgtY = WATER + 40 + rnd(60);
    else f->tgtY = WATER + 30 + rnd(90);
    f->y = f->tgtY * FP;
    f->head = rnd(255); f->spd = CRU[sp]; f->wander = rnd(255);
    f->startle = 0; f->frame = rnd(3); f->frameT = 0;
}

static void initTank(void)
{
    short i;
    nfish = 0;
    for (i = 0; i < 4; i++) addFish(0, i & 1 ? 1 : 2);
    for (i = 0; i < 6; i++) addFish(1, i % 3);
    addFish(2, 2); addFish(2, 1);
    addFish(3, 2);
    addFish(4, 1);
    for (i = 0; i < MAXBUB; i++)  bub[i].on = 0;
    for (i = 0; i < MAXFOOD; i++) food[i].on = 0;
    for (i = 0; i < NKELP; i++) { kelpX[i] = 40 + i*(WIN_W-80)/(NKELP-1); kelpH[i] = 80 + rnd(70); kelpP[i] = rnd(255); }
    snailX = 120; snailDir = 1; tk = 0; paused = false;
}

static void addFood(short px, short py)
{
    short i;
    for (i = 0; i < MAXFOOD; i++)
        if (!food[i].on) { food[i].on = 1; food[i].x = (px + rnd(9) - 4) * FP; food[i].y = py * FP; return; }
}
static short nearestFood(short px, short py)
{
    short i, best = -1; long bd = 999999L;
    for (i = 0; i < MAXFOOD; i++)
        if (food[i].on) {
            short dx = food[i].x - px, dy = food[i].y - py;
            long d = (long)dx*dx + (long)dy*dy;
            if (d < bd) { bd = d; best = i; }
        }
    return best;
}

/* ---- fish behavior (heading-based steering) ---------------------------- */

static void stepFish(short idx)
{
    Fish *f = &fish[idx];
    short px = f->x >> 4, py = f->y >> 4;
    long ddx = 0, ddy = 0;
    short targ = CRU[f->sp], turn = 6, cx, cy, fi;
    long cross, dot;

    if (f->startle > 0) {
        ddx = (f->x - clickX); ddy = (f->y - clickY);
        f->startle--; targ = CRU[f->sp] * 2 + 20; turn = 14;
    } else {
        f->wander += rnd(5) - 2;
        ddx += cosT(f->wander) / 3; ddy += sinT(f->wander) / 3;
        ddy += (long)(f->tgtY - py) * 4;                        /* depth preference */

        fi = nearestFood(f->x, f->y);
        if (fi >= 0) {
            short fdx = (food[fi].x >> 4) - px, fdy = (food[fi].y >> 4) - py;
            ddx += (long)fdx * 6; ddy += (long)fdy * 6;
            if (aabs(fdx) < 6 && aabs(fdy) < 6) food[fi].on = 0;
        }
        if (f->sp <= 1) {                                       /* loose schooling */
            short j, cnt = 0; long hx = 0, hy = 0, cxs = 0, cys = 0;
            for (j = 0; j < nfish; j++) {
                if (j == idx || fish[j].sp != f->sp) continue;
                { short gx = fish[j].x >> 4, gy = fish[j].y >> 4, dx = gx-px, dy = gy-py;
                  long d2 = (long)dx*dx + (long)dy*dy;
                  if (d2 < 2500) { cnt++; hx += cosT(fish[j].head); hy += sinT(fish[j].head);
                                   cxs += dx; cys += dy;
                                   if (d2 < 260 && d2 > 0) { ddx -= (long)dx*6; ddy -= (long)dy*6; } } }
            }
            if (cnt > 0) { ddx += hx / 2 + cxs; ddy += hy / 2 + cys; }
        }
        if (px < 44)         ddx += (long)(44 - px) * 6;        /* walls */
        if (px > WIN_W-44)   ddx -= (long)(px - (WIN_W-44)) * 6;
        if (py < WATER+16)   ddy += (long)(WATER+16 - py) * 6;
        if (py > SUBTOP-8)   ddy -= (long)(py - (SUBTOP-8)) * 6;
    }

    /* steer heading gradually toward (ddx,ddy) */
    cx = cosT(f->head); cy = sinT(f->head);
    cross = (long)cx * ddy - (long)cy * ddx;
    dot   = (long)cx * ddx + (long)cy * ddy;
    if (dot < 0)       f->head += turn;
    else if (cross > 64)  f->head += turn;
    else if (cross < -64) f->head -= turn;

    if (f->spd < targ) f->spd += 2; else if (f->spd > targ) f->spd -= 1;

    f->x += (short)((long)f->spd * cosT(f->head) / 256);
    f->y += (short)((long)f->spd * sinT(f->head) / 256);
    if (f->x < 8*FP)  f->x = 8*FP;   if (f->x > (WIN_W-8)*FP)  f->x = (WIN_W-8)*FP;
    if (f->y < (WATER+8)*FP) f->y = (WATER+8)*FP;
    if (f->y > (SUBTOP-4)*FP) f->y = (SUBTOP-4)*FP;

    f->frameT += f->spd;
    if (f->frameT >= 60) { f->frame = (f->frame + 1) & 3; f->frameT = 0; }
}

static void startleAt(short lx, short ly)
{
    short i;
    clickX = lx * FP; clickY = ly * FP;
    for (i = 0; i < nfish; i++) {
        short dx = (fish[i].x >> 4) - lx, dy = (fish[i].y >> 4) - ly;
        if ((long)dx*dx + (long)dy*dy < 6400) fish[i].startle = 30 + rnd(20);
    }
}

static void stepSim(void)
{
    short i;
    tk++;
    for (i = 0; i < nfish; i++) stepFish(i);

    if (tk % 6 == 0) {                                          /* chest + vents */
        short v; for (v = 0; v < MAXBUB; v++)
            if (!bub[v].on) { bub[v].on = 1;
                short pick = rnd(3);
                bub[v].x = (pick==0?110:pick==1?300:WIN_W-70) + rnd(9)-4;
                bub[v].y = SUBTOP - 2; bub[v].ph = rnd(255); break; }
    }
    for (i = 0; i < MAXBUB; i++)
        if (bub[i].on) { bub[i].y -= 2; bub[i].ph += 16; bub[i].x += sinT(bub[i].ph) / 128;
                         if (bub[i].y < WATER + 2) bub[i].on = 0; }
    for (i = 0; i < MAXFOOD; i++)
        if (food[i].on) { if (tk & 1) food[i].y += FP*3/4;
                          if ((food[i].y >> 4) >= SUBTOP - 1) food[i].on = 0; }
    if (tk % 3 == 0) { snailX += snailDir; if (snailX < 16) snailDir = 1; if (snailX > WIN_W-16) snailDir = -1; }
}

/* ---- fish illustration ------------------------------------------------- */

static void fillOvalP(short l, short t, short r, short b, Pattern *p) { Rect rr; SetRect(&rr,l,t,r,b); FillOval(&rr,p); }

static void drawFish(Fish *f)
{
    short px = f->x >> 4, py = f->y >> 4;
    short dir = (cosT(f->head) >= 0) ? 1 : -1;
    short m = 7 + 3 * f->tier;                          /* size mult /10 */
    short hl = BHL[f->sp]*m/10, hh = BHH[f->sp]*m/10, tl = TL5[f->sp]*m/10;
    short lvl = 2 + f->tier, bk = lvl+1 > 5 ? 5 : lvl+1, be = lvl-2 < 0 ? 0 : lvl-2;
    short wag = (sinT((short)(f->frame * 64)) * hh) / 256;
    short flut = (sinT((short)(f->frame * 64 + 64)) * 3) / 256;
    short backx = px - dir*hl, ex, gx;
    PolyHandle p;

    /* tail */
    p = OpenPoly();
    MoveTo(backx, py);
    LineTo(backx - dir*tl, py - hh - 2 + wag);
    LineTo(backx - dir*tl, py + hh + 2 + wag);
    LineTo(backx, py);
    ClosePoly(); FillPoly(p, &dpat[lvl]); FramePoly(p); KillPoly(p);

    /* dorsal fin */
    p = OpenPoly();
    MoveTo(px - dir*2, py - hh); LineTo(px + dir*2, py - hh - 5 - flut); LineTo(px + dir*6, py - hh);
    LineTo(px - dir*2, py - hh); ClosePoly(); FillPoly(p, &dpat[be]); FramePoly(p); KillPoly(p);
    /* pectoral fin */
    p = OpenPoly();
    MoveTo(px + dir*(hl/4), py + hh/3); LineTo(px + dir*(hl/4) - dir*4, py + hh/2 + 4 + flut);
    LineTo(px + dir*(hl/4) + dir*3, py + hh/2); ClosePoly(); FillPoly(p, &dpat[be]); KillPoly(p);

    /* body: base fill, darker back, lighter belly, outline */
    fillOvalP(px-hl, py-hh, px+hl, py+hh, &dpat[lvl]);
    fillOvalP(px-hl+2, py-hh, px+hl-2, py, &dpat[bk]);
    fillOvalP(px-hl+3, py+hh/2, px+hl-3, py+hh, &dpat[be]);
    { Rect body; SetRect(&body, px-hl, py-hh, px+hl, py+hh); FrameOval(&body); }

    /* species markings */
    if (f->sp == 1) {                                  /* tetra: stripe + tail spot */
        Rect s; SetRect(&s, px-hl+2, py-1, px+hl-4, py+1); FillRect(&s, &dpat[5]);
        SetRect(&s, backx+dir*1, py-2, backx+dir*3, py+2); FillOval(&s, &dpat[0]);
    } else if (f->sp == 2) {                            /* angel: vertical bands */
        short bxi; for (bxi = -1; bxi <= 1; bxi++) { Rect s; SetRect(&s, px+bxi*hl/2-1, py-hh+2, px+bxi*hl/2+1, py+hh-2); FillRect(&s, &dpat[5]); }
    } else if (f->sp == 3) {                            /* puffer: spots + spikes */
        short a; for (a = 0; a < 5; a++) { short sxp = px + rnd(hl)-hl/2, syp = py + rnd(hh)-hh/2; Rect s; SetRect(&s, sxp, syp, sxp+2, syp+2); FillOval(&s, &dpat[5]); }
        for (a = 0; a < 8; a++) { short ang = a*32; MoveTo(px + cosT(ang)*hl/256, py + sinT(ang)*hh/256); LineTo(px + cosT(ang)*(hl+3)/256, py + sinT(ang)*(hh+3)/256); }
    } else if (f->sp == 4) {                            /* catfish: whiskers */
        ex = px + dir*(hl-2);
        MoveTo(ex, py); LineTo(ex + dir*6, py - 4);
        MoveTo(ex, py+2); LineTo(ex + dir*6, py + 5);
    }

    /* gill line */
    gx = px + dir*(hl/3);
    MoveTo(gx, py-hh+2); LineTo(gx - dir*2, py); LineTo(gx, py+hh-2);

    /* eye */
    ex = px + dir*(hl-4);
    { Rect e; SetRect(&e, ex-2, py-3, ex+3, py+2); FillOval(&e, &dpat[0]); FrameOval(&e);
      SetRect(&e, ex, py-1, ex+2, py+1); PaintOval(&e); }
}

/* ---- static background ------------------------------------------------- */

static void renderBackground(void)
{
    short i, y;
    Rect r;
    SetPort(&gBg);
    PenNormal();
    r = gBgBits.bounds; EraseRect(&r);

    /* water darkens with depth (banded dither) */
    for (i = 0; i < 5; i++) {
        short lvl = i * 1 / 2;                          /* 0,0,1,1,2 */
        SetRect(&r, 0, WATER + i*(SUBTOP-WATER)/5, WIN_W, WATER + (i+1)*(SUBTOP-WATER)/5);
        if (lvl > 0) FillRect(&r, &dpat[lvl]);
    }
    /* background plants (light, static, behind fish) */
    for (i = 0; i < 4; i++) {
        short bx = 90 + i*90, seg;
        PenSize(2,2);
        MoveTo(bx, SUBTOP);
        for (seg = 1; seg <= 12; seg++) { short ny = SUBTOP - seg*7, nx = bx + ((seg*seg) % 11) - 5; LineTo(nx, ny); MoveTo(nx, ny); }
        PenSize(1,1);
    }
    /* gravel + rocks */
    SetRect(&r, 0, SUBTOP, WIN_W, WIN_H); FillRect(&r, &dpat[3]);
    MoveTo(0, SUBTOP); LineTo(WIN_W, SUBTOP);
    { Rect rk; SetRect(&rk, 60, SUBTOP-8, 140, SUBTOP+8); FillOval(&rk, &dpat[4]); FrameOval(&rk);
      SetRect(&rk, 320, SUBTOP-6, 380, SUBTOP+8); FillOval(&rk, &dpat[4]); FrameOval(&rk); }
    /* treasure chest, bottom right */
    { short cx0 = WIN_W-90, cy0 = SUBTOP-16; Rect box;
      SetRect(&box, cx0, cy0, cx0+40, cy0+18); FillRect(&box, &dpat[4]); FrameRect(&box);
      SetRect(&box, cx0, cy0-8, cx0+40, cy0+2); FrameRect(&box);           /* lid */
      MoveTo(cx0, cy0-3); LineTo(cx0+40, cy0-3);
      SetRect(&box, cx0+17, cy0-2, cx0+23, cy0+6); FillRect(&box, &dpat[0]); FrameRect(&box); } /* lock */
}

/* ---- per-frame render -------------------------------------------------- */

static void drawKelp(short rx, short h, short ph)
{
    short seg, segs = h/8, nx, ny;
    PenSize(3,3);
    MoveTo(rx, SUBTOP);
    for (seg = 1; seg <= segs; seg++) { ny = SUBTOP - seg*8; nx = rx + sinT((short)(tk/2 + ph + seg*10)) * seg / 90; LineTo(nx, ny); MoveTo(nx, ny); }
    PenSize(1,1);
}

static void render(void)
{
    short i, t;
    Rect r = gOffBits.bounds;

    SetPort(&gOff);
    PenNormal();
    CopyBits(&gBgBits, &gOffBits, &r, &r, srcCopy, NULL);      /* static bg */

    /* drifting light shafts */
    for (i = 0; i < 2; i++) {
        short bx = (short)(((long)i*230 + tk/3) % (WIN_W+140)) - 70;
        PolyHandle p = OpenPoly();
        MoveTo(bx, WATER); LineTo(bx+24, WATER); LineTo(bx+58, SUBTOP); LineTo(bx+18, SUBTOP); LineTo(bx, WATER);
        ClosePoly(); FillPoly(p, &dpat[1]); KillPoly(p);
    }
    /* surface ripple */
    { short x, py; MoveTo(0, WATER); for (x = 0; x <= WIN_W; x += 8) { py = WATER + sinT((short)(x + tk))/90; LineTo(x, py); } }

    for (i = 0; i < NKELP; i++) drawKelp(kelpX[i], kelpH[i], kelpP[i]);

    { Rect sh; SetRect(&sh, snailX-4, SUBTOP-5, snailX+4, SUBTOP+1); FillOval(&sh,&dpat[4]); FrameOval(&sh);
      MoveTo(snailX - snailDir*4, SUBTOP+1); LineTo(snailX + snailDir*6, SUBTOP+1); }

    for (i = 0; i < MAXFOOD; i++)
        if (food[i].on) { short fx = food[i].x>>4, fy = food[i].y>>4; SetRect(&r, fx, fy, fx+2, fy+2); PaintRect(&r); }

    /* fish, far tier first */
    for (t = 0; t < 3; t++)
        for (i = 0; i < nfish; i++)
            if (fish[i].tier == t) drawFish(&fish[i]);

    /* foreground bubbles */
    for (i = 0; i < MAXBUB; i++)
        if (bub[i].on) { short r0 = 2 + (bub[i].ph & 1); SetRect(&r, bub[i].x-r0, bub[i].y-r0, bub[i].x+r0, bub[i].y+r0); FrameOval(&r); }

    FrameRect(&gOffBits.bounds);
}

/* ---- main -------------------------------------------------------------- */

int main(void)
{
    WindowPtr win; EventRecord ev; Rect r; long nextFrame;
    short sw, sh, winL, winT, i;
    unsigned char L0[8]={0,0,0,0,0,0,0,0};
    unsigned char L1[8]={0x00,0x44,0x00,0x11,0x00,0x44,0x00,0x11};
    unsigned char L2[8]={0x88,0x22,0x88,0x22,0x88,0x22,0x88,0x22};
    unsigned char L3[8]={0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55};
    unsigned char L4[8]={0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD};
    unsigned char L5[8]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    InitGraf(&qd.thePort);
    InitFonts(); InitWindows(); InitMenus(); TEInit(); InitDialogs(NULL); InitCursor();

    sw = qd.screenBits.bounds.right; sh = qd.screenBits.bounds.bottom;
    winL = (sw - WIN_W) / 2; if (winL < 4) winL = 4;
    winT = (sh - WIN_H) / 2 + 8; if (winT < 40) winT = 40;
    SetRect(&r, winL, winT, winL + WIN_W, winT + WIN_H);
    win = NewWindow(NULL, &r, "\pAquarium", true, documentProc, (WindowPtr)-1L, true, 0);
    SetPort(win);

    buildSin();
    setPat(&dpat[0],L0); setPat(&dpat[1],L1); setPat(&dpat[2],L2);
    setPat(&dpat[3],L3); setPat(&dpat[4],L4); setPat(&dpat[5],L5);
    if (!makeBuf(&gBg, &gBgBits))  { SysBeep(20); return 1; }
    if (!makeBuf(&gOff, &gOffBits)) { SysBeep(20); return 1; }
    renderBackground();
    initTank();

    nextFrame = TickCount();
    for (;;) {
        if (WaitNextEvent(everyEvent, &ev, 2, NULL)) {
            if (ev.what == keyDown || ev.what == autoKey) {
                char c = (char)(ev.message & charCodeMask);
                if ((ev.modifiers & cmdKey) && (c == 'q' || c == 'Q')) break;
                else if (c == ' ') paused = !paused;
                else if (c == 'r' || c == 'R') initTank();
            } else if (ev.what == mouseDown) {
                WindowPtr wp; short part = FindWindow(ev.where, &wp);
                if (part == inGoAway && TrackGoAway(wp, ev.where)) break;
                else if (part == inDrag) DragWindow(wp, ev.where, &qd.screenBits.bounds);
                else if (part == inContent && wp == win) {
                    Point p = ev.where; SetPort(win); GlobalToLocal(&p);
                    startleAt(p.h, p.v);
                    for (i = 0; i < 6; i++) addFood(p.h, WATER + 4);
                }
            } else if (ev.what == updateEvt) {
                BeginUpdate((WindowPtr)ev.message); blit(win); EndUpdate((WindowPtr)ev.message);
            }
        }
        if (TickCount() >= nextFrame) {
            nextFrame = TickCount() + 3;
            if (!paused) stepSim();
            render(); blit(win);
        }
    }

    if (gOffBits.baseAddr) DisposePtr(gOffBits.baseAddr);
    if (gBgBits.baseAddr)  DisposePtr(gBgBits.baseAddr);
    DisposeWindow(win);
    return 0;
}
