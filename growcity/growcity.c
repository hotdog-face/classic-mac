/*
 * GrowCity - a growing isometric city simulation for the Classic Macintosh
 * ------------------------------------------------------------------------
 * A little town seeds itself at the center, sprouts roads outward, raises
 * dithered 3D buildings that grow taller over time, and fills with tiny
 * people who wander the streets. Black & white, drawn to an offscreen
 * bitmap for flicker-free animation. Made for a Mac Plus / Mini vMac.
 *
 *   Click   - drop a new road seed where you click (start a district)
 *   Space   - pause / resume
 *   R       - reset the city
 *   Cmd-Q   - quit
 *
 * Built with Retro68 (https://github.com/autc04/Retro68).
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
#include <stdio.h>

#define GRID   18
#define TILEW  22
#define TILEH  11
#define TW2    (TILEW/2)
#define TH2    (TILEH/2)
#define WIN_W  480
#define WIN_H  310
#define MAXPPL 40
#define STEP   8

enum { T_GRASS = 0, T_ROAD, T_BLDG, T_PARK };

typedef struct { unsigned char type, h, th, style; } Tile;
static Tile g[GRID][GRID];

typedef struct { short ci, cj, ti, tj, t; unsigned char on; } Person;
static Person ppl[MAXPPL];
static short  nppl = 0;

static short   OX = WIN_W / 2, OY = 54;
static long    gday = 0, gtick = 0;
static Boolean paused = false;

static GrafPort gOff;
static BitMap   gOffBits;
static Pattern  patGrass, patRoad, patWin, patBrick;

/* ---- small helpers ---------------------------------------------------- */

static unsigned long rs = 22695477UL;
static short rnd(short n)
{
    rs = rs * 1103515245UL + 12345UL;
    return (short)(((rs >> 16) & 0x7fff) % (n > 0 ? n : 1));
}

static void setPat(Pattern *p, const unsigned char *b) { memcpy(p, b, 8); }

static void isoC(short i, short j, short *cx, short *cy)
{
    *cx = OX + (i - j) * TW2;
    *cy = OY + (i + j) * TH2;
}

static void initPatterns(void)
{
    unsigned char grass[8] = {0x88,0x00,0x22,0x00,0x88,0x00,0x22,0x00};
    unsigned char road[8]  = {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55};
    unsigned char win[8]   = {0xFF,0x81,0xBD,0x81,0xFF,0x81,0xBD,0x81};
    unsigned char brick[8] = {0xFF,0x11,0x11,0x11,0xFF,0x11,0x11,0x11};
    setPat(&patGrass, grass); setPat(&patRoad, road);
    setPat(&patWin, win);     setPat(&patBrick, brick);
}

/* ---- offscreen buffer -------------------------------------------------- */

static Boolean makeOffscreen(void)
{
    Rect b; short rb; long sz;
    SetRect(&b, 0, 0, WIN_W, WIN_H);
    rb = ((WIN_W + 15) / 16) * 2;          /* even rowBytes */
    sz = (long)rb * WIN_H;
    gOffBits.baseAddr = NewPtr(sz);
    if (gOffBits.baseAddr == NULL) return false;
    gOffBits.rowBytes = rb;
    gOffBits.bounds   = b;
    OpenPort(&gOff);
    SetPortBits(&gOffBits);
    gOff.portRect = b;
    ClipRect(&b);
    RectRgn(gOff.visRgn, &b);
    EraseRect(&b);
    return true;
}

static void blit(WindowPtr w)
{
    Rect s = gOffBits.bounds;
    SetPort(w);
    CopyBits(&gOffBits, &w->portBits, &s, &w->portRect, srcCopy, NULL);
}

/* ---- drawing ----------------------------------------------------------- */

static void drawDiamond(short cx, short cy, short lift, Pattern *pat)
{
    PolyHandle p = OpenPoly();
    MoveTo(cx, cy - TH2 - lift);
    LineTo(cx + TW2, cy - lift);
    LineTo(cx, cy + TH2 - lift);
    LineTo(cx - TW2, cy - lift);
    LineTo(cx, cy - TH2 - lift);
    ClosePoly();
    FillPoly(p, pat);
    FramePoly(p);
    KillPoly(p);
}

static void drawTree(short cx, short cy)
{
    Rect c;
    MoveTo(cx, cy); LineTo(cx, cy - 5);            /* trunk */
    SetRect(&c, cx - 3, cy - 11, cx + 4, cy - 4);  /* canopy */
    PaintOval(&c);
}

static void drawBuilding(short cx, short cy, short h, short style)
{
    PolyHandle p, q;
    Pattern *side;
    if (style == 0)      side = &patWin;
    else if (style == 1) side = &patBrick;
    else                 side = &qd.gray;

    /* right (lit) face */
    p = OpenPoly();
    MoveTo(cx + TW2, cy);
    LineTo(cx, cy + TH2);
    LineTo(cx, cy + TH2 - h);
    LineTo(cx + TW2, cy - h);
    LineTo(cx + TW2, cy);
    ClosePoly(); FillPoly(p, side); FramePoly(p); KillPoly(p);

    /* left (shadow) face */
    p = OpenPoly();
    MoveTo(cx - TW2, cy);
    LineTo(cx, cy + TH2);
    LineTo(cx, cy + TH2 - h);
    LineTo(cx - TW2, cy - h);
    LineTo(cx - TW2, cy);
    ClosePoly(); FillPoly(p, &qd.dkGray); FramePoly(p); KillPoly(p);

    /* roof */
    q = OpenPoly();
    MoveTo(cx, cy - TH2 - h);
    LineTo(cx + TW2, cy - h);
    LineTo(cx, cy + TH2 - h);
    LineTo(cx - TW2, cy - h);
    LineTo(cx, cy - TH2 - h);
    ClosePoly(); FillPoly(q, &qd.ltGray); FramePoly(q); KillPoly(q);
}

static void drawPerson(short x, short y, short phase)
{
    Rect hd;
    PenSize(1, 1);
    MoveTo(x, y - 4); LineTo(x, y - 1);                /* body */
    SetRect(&hd, x - 1, y - 6, x + 2, y - 3);          /* head */
    PaintOval(&hd);
    if (phase) { MoveTo(x, y - 1); LineTo(x - 1, y + 1); MoveTo(x, y - 1); LineTo(x + 2, y + 1); }
    else       { MoveTo(x, y - 1); LineTo(x - 2, y + 1); MoveTo(x, y - 1); LineTo(x + 1, y + 1); }
}

/* ---- simulation -------------------------------------------------------- */

static void initCity(void)
{
    short i, j, c = GRID / 2;
    for (i = 0; i < GRID; i++)
        for (j = 0; j < GRID; j++) {
            g[i][j].type = T_GRASS; g[i][j].h = 0; g[i][j].th = 0; g[i][j].style = 0;
        }
    g[c][c].type = T_ROAD;
    g[c+1][c].type = T_ROAD; g[c-1][c].type = T_ROAD;
    g[c][c+1].type = T_ROAD; g[c][c-1].type = T_ROAD;
    nppl = 0; gday = 0; gtick = 0; paused = false;
}

static void seedAt(short mx, short my)
{
    short i, j, bi = -1, bj = -1;
    long best = 0x7fffffffL;
    for (i = 0; i < GRID; i++)
        for (j = 0; j < GRID; j++) {
            short cx, cy; long dx, dy, d;
            isoC(i, j, &cx, &cy);
            dx = mx - cx; dy = my - cy; d = dx * dx + dy * dy;
            if (d < best) { best = d; bi = i; bj = j; }
        }
    if (bi >= 0 && (g[bi][bj].type == T_GRASS || g[bi][bj].type == T_PARK))
        g[bi][bj].type = T_ROAD;
}

static long countBuildings(void)
{
    short i, j; long b = 0;
    for (i = 0; i < GRID; i++)
        for (j = 0; j < GRID; j++)
            if (g[i][j].type == T_BLDG) b++;
    return b;
}

static short roadNbr(short i, short j, short *ni, short *nj)
{
    short cand = 0, ci[4], cj[4], k;
    if (i + 1 < GRID && g[i+1][j].type == T_ROAD) { ci[cand] = i+1; cj[cand] = j; cand++; }
    if (i - 1 >= 0   && g[i-1][j].type == T_ROAD) { ci[cand] = i-1; cj[cand] = j; cand++; }
    if (j + 1 < GRID && g[i][j+1].type == T_ROAD) { ci[cand] = i; cj[cand] = j+1; cand++; }
    if (j - 1 >= 0   && g[i][j-1].type == T_ROAD) { ci[cand] = i; cj[cand] = j-1; cand++; }
    if (cand == 0) return 0;
    k = rnd(cand); *ni = ci[k]; *nj = cj[k];
    return 1;
}

static void spawnPeople(void)
{
    long bld = countBuildings();
    short cap = (short)(bld / 2);
    if (cap > MAXPPL) cap = MAXPPL;
    if (nppl < cap && (gtick & 3) == 0) {
        short tries = 20;
        while (tries--) {
            short i = rnd(GRID), j = rnd(GRID);
            if (g[i][j].type == T_ROAD) {
                ppl[nppl].ci = i; ppl[nppl].cj = j;
                ppl[nppl].ti = i; ppl[nppl].tj = j;
                ppl[nppl].t = 0; ppl[nppl].on = 1; nppl++;
                break;
            }
        }
    }
}

static void movePeople(void)
{
    short k;
    for (k = 0; k < nppl; k++) {
        if (!ppl[k].on) continue;
        ppl[k].t++;
        if (ppl[k].t >= STEP) {
            short ni, nj;
            ppl[k].ci = ppl[k].ti; ppl[k].cj = ppl[k].tj; ppl[k].t = 0;
            if (roadNbr(ppl[k].ci, ppl[k].cj, &ni, &nj)) { ppl[k].ti = ni; ppl[k].tj = nj; }
            else { ppl[k].ti = ppl[k].ci; ppl[k].tj = ppl[k].cj; }
        }
    }
}

static void stepSim(void)
{
    short i, j, c = GRID / 2;
    gtick++;

    /* buildings rise smoothly */
    for (i = 0; i < GRID; i++)
        for (j = 0; j < GRID; j++)
            if (g[i][j].type == T_BLDG && g[i][j].h < g[i][j].th && (gtick & 1))
                g[i][j].h++;

    /* structural growth every few ticks */
    if (gtick % 6 == 0) {
        short tries = 6;
        while (tries--) {                       /* extend roads */
            i = rnd(GRID); j = rnd(GRID);
            if (g[i][j].type == T_ROAD) {
                short d = rnd(4), ni = i, nj = j;
                if (d == 0) ni++; else if (d == 1) ni--; else if (d == 2) nj++; else nj--;
                if (ni >= 0 && ni < GRID && nj >= 0 && nj < GRID &&
                    g[ni][nj].type == T_GRASS && rnd(100) < 70)
                    g[ni][nj].type = T_ROAD;
            }
        }
        for (i = 0; i < GRID; i++)              /* sprout buildings by roads */
            for (j = 0; j < GRID; j++) {
                if (g[i][j].type != T_GRASS) continue;
                {
                    short adj = 0;
                    if (i+1 < GRID && g[i+1][j].type == T_ROAD) adj = 1;
                    if (i-1 >= 0   && g[i-1][j].type == T_ROAD) adj = 1;
                    if (j+1 < GRID && g[i][j+1].type == T_ROAD) adj = 1;
                    if (j-1 >= 0   && g[i][j-1].type == T_ROAD) adj = 1;
                    if (adj && rnd(100) < 10) {
                        if (rnd(100) < 12) {
                            g[i][j].type = T_PARK;
                        } else {
                            short dist = (i > c ? i - c : c - i) + (j > c ? j - c : c - j);
                            short th = 34 - dist * 2; if (th < 5) th = 5;
                            g[i][j].type = T_BLDG; g[i][j].th = th; g[i][j].h = 1;
                            g[i][j].style = rnd(3);
                        }
                    }
                }
            }
        gday++;
    }

    spawnPeople();
    movePeople();
}

/* ---- render ------------------------------------------------------------ */

static void render(void)
{
    short i, j, sum, cx, cy, k;
    Rect r, hr;
    char buf[96];
    long bld;

    SetPort(&gOff);
    PenNormal();
    r = gOffBits.bounds;
    EraseRect(&r);                              /* white sky */

    for (sum = 0; sum <= 2 * (GRID - 1); sum++) {
        for (i = 0; i < GRID; i++) {
            j = sum - i;
            if (j < 0 || j >= GRID) continue;
            isoC(i, j, &cx, &cy);

            if (g[i][j].type == T_ROAD)      drawDiamond(cx, cy, 0, &patRoad);
            else if (g[i][j].type == T_PARK) { drawDiamond(cx, cy, 0, &patGrass); drawTree(cx, cy); }
            else                             drawDiamond(cx, cy, 0, &patGrass);

            for (k = 0; k < nppl; k++)
                if (ppl[k].on && ppl[k].ci == i && ppl[k].cj == j) {
                    short x0, y0, x1, y1, px, py;
                    isoC(ppl[k].ci, ppl[k].cj, &x0, &y0);
                    isoC(ppl[k].ti, ppl[k].tj, &x1, &y1);
                    px = x0 + (short)((long)(x1 - x0) * ppl[k].t / STEP);
                    py = y0 + (short)((long)(y1 - y0) * ppl[k].t / STEP);
                    drawPerson(px, py, (short)((ppl[k].t >> 1) & 1));
                }

            if (g[i][j].type == T_BLDG && g[i][j].h > 0)
                drawBuilding(cx, cy, g[i][j].h, g[i][j].style);
        }
    }

    bld = countBuildings();
    SetRect(&hr, 0, 0, WIN_W, 20);
    EraseRect(&hr);
    MoveTo(8, 14);
    sprintf(buf, "GrowCity   Day %ld    People %d    Buildings %ld%s",
            gday, nppl, bld, paused ? "    [paused]" : "");
    DrawText(buf, 0, (short)strlen(buf));
    MoveTo(0, 19); LineTo(WIN_W, 19);
}

/* ---- main -------------------------------------------------------------- */

int main(void)
{
    WindowPtr win;
    EventRecord ev;
    Rect r;
    long nextFrame;
    short sw, sh, winL, winT;

    InitGraf(&qd.thePort);
    InitFonts(); InitWindows(); InitMenus(); TEInit(); InitDialogs(NULL); InitCursor();

    sw = qd.screenBits.bounds.right;
    sh = qd.screenBits.bounds.bottom;
    winL = (sw - WIN_W) / 2; if (winL < 4) winL = 4;
    winT = (sh - WIN_H) / 2 + 8; if (winT < 40) winT = 40;
    SetRect(&r, winL, winT, winL + WIN_W, winT + WIN_H);
    win = NewWindow(NULL, &r, "\pGrowCity", true, documentProc, (WindowPtr)-1L, true, 0);
    SetPort(win);

    initPatterns();
    if (!makeOffscreen()) { SysBeep(20); return 1; }
    initCity();

    nextFrame = TickCount();
    for (;;) {
        if (WaitNextEvent(everyEvent, &ev, 2, NULL)) {
            if (ev.what == keyDown || ev.what == autoKey) {
                char c = (char)(ev.message & charCodeMask);
                if ((ev.modifiers & cmdKey) && (c == 'q' || c == 'Q')) break;
                else if (c == ' ') paused = !paused;
                else if (c == 'r' || c == 'R') initCity();
            } else if (ev.what == mouseDown) {
                WindowPtr wp;
                short part = FindWindow(ev.where, &wp);
                if (part == inGoAway && TrackGoAway(wp, ev.where)) break;
                else if (part == inDrag) DragWindow(wp, ev.where, &qd.screenBits.bounds);
                else if (part == inContent && wp == win) {
                    Point p = ev.where;
                    SetPort(win);
                    GlobalToLocal(&p);
                    seedAt(p.h, p.v);
                }
            } else if (ev.what == updateEvt) {
                BeginUpdate((WindowPtr)ev.message);
                blit(win);
                EndUpdate((WindowPtr)ev.message);
            }
        }
        if (TickCount() >= nextFrame) {
            nextFrame = TickCount() + 3;         /* cap the pace */
            if (!paused) stepSim();
            render();
            blit(win);
        }
    }

    if (gOffBits.baseAddr) DisposePtr(gOffBits.baseAddr);
    DisposeWindow(win);
    return 0;
}
