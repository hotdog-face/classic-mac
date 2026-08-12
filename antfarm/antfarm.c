/*
 * AntFarm - a formicarium simulation for the Classic Macintosh
 * ------------------------------------------------------------
 * A side-view ant colony. Ants dig branching tunnels out of the soil,
 * keep a central nest, and send foragers up to the surface to collect
 * food crumbs and haul them home. Every few crumbs, a new ant is born,
 * so the colony (and its tunnel network) slowly grows.
 *
 *   Click  - drop a food crumb on the surface at that spot
 *   Space  - pause / resume
 *   R      - reset the colony
 *   Cmd-Q  - quit
 *
 * Black & white, drawn to an offscreen bitmap. Built with Retro68.
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

#define CELL     6
#define GW       76
#define GH       50
#define WIN_W    (GW*CELL)      /* 456 */
#define WIN_H    (GH*CELL)      /* 300 */
#define SURF     5              /* rows 0..4 = air, row 5.. = dirt        */
#define SURFWALK (SURF-1)       /* ants walk the surface on this air row  */
#define MAXANT   64
#define MAXCRUMB 32

enum { AIR = 0, DIRT, TUN, CHAMBER };
enum { A_DIG = 0, A_UP, A_SEEK, A_HOME_S, A_HOME_U };

typedef struct { short x, y, gx, gy; unsigned char st, carry, tmr; } Ant;
typedef struct { short x; unsigned char on; } Crumb;

static unsigned char soil[GH][GW];
static Ant   ant[MAXANT];  static short nant = 0;
static Crumb crumb[MAXCRUMB]; static short ncrumb = 0;

static short nestX, nestY;
static long  foodStore = 0, births = 0, gday = 0, gtick = 0;
static long  digCount = 0, digCap = 0;
static Boolean paused = false;

static GrafPort gOff;
static BitMap   gOffBits;
static Pattern  patDirt;

static unsigned long rs = 987654321UL;
static short rnd(short n) { rs = rs*1103515245UL + 12345UL; return (short)(((rs>>16)&0x7fff)%(n>0?n:1)); }
static short sgn(short a) { return a>0 ? 1 : (a<0 ? -1 : 0); }
static void  setPat(Pattern *p, const unsigned char *b) { memcpy(p, b, 8); }

/* ---- offscreen --------------------------------------------------------- */

static Boolean makeOffscreen(void)
{
    Rect b; short rb; long sz;
    SetRect(&b, 0, 0, WIN_W, WIN_H);
    rb = ((WIN_W + 15) / 16) * 2;
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

/* ---- world ------------------------------------------------------------- */

static Boolean openU(short x, short y)
{
    return (x >= 0 && x < GW && y >= SURF && y < GH &&
            (soil[y][x] == TUN || soil[y][x] == CHAMBER));
}

static void initFarm(void)
{
    short x, y;
    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++)
            soil[y][x] = (y < SURF) ? AIR : DIRT;

    nestX = GW / 2; nestY = SURF + 8;
    for (y = SURF; y <= nestY; y++) soil[y][nestX] = TUN;         /* shaft   */
    for (y = nestY - 1; y <= nestY + 1; y++)                      /* chamber */
        for (x = nestX - 2; x <= nestX + 2; x++)
            if (x >= 0 && x < GW && y >= SURF && y < GH) soil[y][x] = CHAMBER;

    digCount = 0;
    for (y = SURF; y < GH; y++)
        for (x = 0; x < GW; x++)
            if (soil[y][x] != DIRT) digCount++;
    digCap = (long)(GH - SURF) * GW * 38 / 100;

    nant = 0;
    for (x = 0; x < 6; x++) {
        ant[nant].x = nestX; ant[nant].y = nestY;
        ant[nant].gx = 0; ant[nant].gy = 0;
        ant[nant].st = A_DIG; ant[nant].carry = 0; ant[nant].tmr = (unsigned char)rnd(30);
        nant++;
    }
    ncrumb = 0; foodStore = 0; births = 0; gday = 0; gtick = 0; paused = false;
}

static void addCrumb(short cx)
{
    short i;
    if (cx < 0) cx = 0; if (cx >= GW) cx = GW - 1;
    for (i = 0; i < ncrumb; i++)
        if (!crumb[i].on) { crumb[i].x = cx; crumb[i].on = 1; return; }
    if (ncrumb < MAXCRUMB) { crumb[ncrumb].x = cx; crumb[ncrumb].on = 1; ncrumb++; }
}

/* move one step toward (tx,ty); dig soft dirt if allowed and blocked */
static void moveDig(Ant *a, short tx, short ty, Boolean mayDig)
{
    short dx = sgn(tx - a->x), dy = sgn(ty - a->y);
    short adx = (tx > a->x) ? tx - a->x : a->x - tx;
    short ady = (ty > a->y) ? ty - a->y : a->y - ty;
    short cx[4], cy[4], nc = 0, i;

    if (adx >= ady) {
        if (dx) { cx[nc] = a->x + dx; cy[nc] = a->y; nc++; }
        if (dy) { cx[nc] = a->x; cy[nc] = a->y + dy; nc++; }
    } else {
        if (dy) { cx[nc] = a->x; cy[nc] = a->y + dy; nc++; }
        if (dx) { cx[nc] = a->x + dx; cy[nc] = a->y; nc++; }
    }
    cx[nc] = a->x - 1; cy[nc] = a->y; nc++;
    cx[nc] = a->x + 1; cy[nc] = a->y; nc++;

    for (i = 0; i < nc; i++)
        if (openU(cx[i], cy[i])) { a->x = cx[i]; a->y = cy[i]; return; }

    if (mayDig) {
        short px, py;
        if (adx >= ady) { px = a->x + dx; py = a->y; }
        else            { px = a->x; py = a->y + dy; }
        if (px >= 0 && px < GW && py >= SURF && py < GH && soil[py][px] == DIRT) {
            soil[py][px] = TUN; digCount++; a->x = px; a->y = py; return;
        }
        px = a->x; py = a->y + 1;   /* fall back to digging down */
        if (py < GH && soil[py][px] == DIRT) { soil[py][px] = TUN; digCount++; a->y = py; return; }
    }

    { short d = rnd(4);
      short nx = a->x + (d == 0 ? 1 : d == 1 ? -1 : 0);
      short ny = a->y + (d == 2 ? 1 : d == 3 ? -1 : 0);
      if (openU(nx, ny)) { a->x = nx; a->y = ny; } }
}

static short nearestCrumb(short ax)
{
    short i, best = -1, bd = 32767;
    for (i = 0; i < ncrumb; i++)
        if (crumb[i].on) {
            short d = (crumb[i].x > ax) ? crumb[i].x - ax : ax - crumb[i].x;
            if (d < bd) { bd = d; best = i; }
        }
    return best;
}

static void birth(void)
{
    if (foodStore >= (births + 1) * 4 && nant < MAXANT) {
        births++;
        ant[nant].x = nestX; ant[nant].y = nestY; ant[nant].gx = 0; ant[nant].gy = 0;
        ant[nant].st = A_DIG; ant[nant].carry = 0; ant[nant].tmr = 0;
        nant++;
    }
}

static void stepAnt(Ant *a)
{
    a->tmr++;
    switch (a->st) {
    case A_DIG:
        if (a->gx == 0 && a->gy == 0) { a->gx = rnd(GW); a->gy = SURF + rnd(GH - SURF); }
        moveDig(a, a->gx, a->gy, digCount < digCap && (a->tmr & 1));
        if (rnd(60) == 0) { a->gx = 0; a->gy = 0; }        /* pick a new spot  */
        if (rnd(240) == 0) { a->st = A_UP; a->tmr = 0; }   /* go foraging      */
        break;
    case A_UP:
        moveDig(a, nestX, SURF, true);
        if (a->y <= SURF) { a->y = SURFWALK; a->st = A_SEEK; a->tmr = 0; }
        break;
    case A_SEEK: {
        short c = nearestCrumb(a->x);
        if (c < 0) { if (a->tmr > 140) a->st = A_HOME_S; else a->x += rnd(3) - 1; }
        else {
            a->x += sgn(crumb[c].x - a->x);
            if (a->x == crumb[c].x) { crumb[c].on = 0; a->carry = 1; a->st = A_HOME_S; }
        }
        if (a->x < 0) a->x = 0; if (a->x >= GW) a->x = GW - 1;
        break;
    }
    case A_HOME_S:
        a->x += sgn(nestX - a->x);
        if (a->x == nestX) { a->y = SURF; a->st = A_HOME_U; }
        break;
    case A_HOME_U:
        moveDig(a, nestX, nestY, false);
        { short ddx = (a->x > nestX) ? a->x - nestX : nestX - a->x;
          short ddy = (a->y > nestY) ? a->y - nestY : nestY - a->y;
          if (ddx <= 2 && ddy <= 1) {
              if (a->carry) { a->carry = 0; foodStore++; birth(); }
              a->st = A_DIG; a->gx = 0; a->gy = 0; a->tmr = 0;
          } }
        break;
    }
    if (a->y < 0) a->y = 0; if (a->y >= GH) a->y = GH - 1;
}

static void stepSim(void)
{
    short i;
    gtick++;
    for (i = 0; i < nant; i++) stepAnt(&ant[i]);
    if (gtick % 55 == 0) addCrumb(rnd(GW));
    if (gtick % 90 == 0) gday++;
}

/* ---- render ------------------------------------------------------------ */

static void drawAnt(short px, short py, short carry)
{
    Rect b;
    SetRect(&b, px - 1, py - 1, px + 1, py + 2); PaintRect(&b);   /* body */
    MoveTo(px - 2, py); LineTo(px + 2, py);                        /* legs */
    MoveTo(px, py - 3); LineTo(px, py - 2);                        /* head */
    if (carry) { SetRect(&b, px - 1, py - 6, px + 2, py - 3); PaintRect(&b); }
}

static void render(void)
{
    short x, y, px, py, i;
    Rect r, cellR, hud;
    char buf[96];

    SetPort(&gOff);
    PenNormal();
    r = gOffBits.bounds;
    EraseRect(&r);                                   /* sky */

    SetRect(&r, 0, SURF * CELL, WIN_W, WIN_H);
    FillRect(&r, &patDirt);                           /* soil */

    for (y = SURF; y < GH; y++)                        /* carve open cells */
        for (x = 0; x < GW; x++)
            if (soil[y][x] == TUN || soil[y][x] == CHAMBER) {
                SetRect(&cellR, x * CELL, y * CELL, x * CELL + CELL, y * CELL + CELL);
                EraseRect(&cellR);
            }

    MoveTo(0, SURF * CELL); LineTo(WIN_W, SURF * CELL);            /* ground line */
    for (x = 4; x < GW; x += 7) {                                  /* grass tufts */
        px = x * CELL;
        MoveTo(px, SURF * CELL); LineTo(px - 1, SURF * CELL - 3);
        MoveTo(px, SURF * CELL); LineTo(px + 1, SURF * CELL - 3);
    }

    for (i = 0; i < ncrumb; i++)                                   /* food crumbs */
        if (crumb[i].on) {
            SetRect(&cellR, crumb[i].x * CELL + 1, SURFWALK * CELL + 1,
                            crumb[i].x * CELL + 4, SURFWALK * CELL + 4);
            PaintRect(&cellR);
        }

    for (i = 0; i < nant; i++) {                                   /* ants */
        px = ant[i].x * CELL + CELL / 2;
        py = ant[i].y * CELL + CELL / 2;
        drawAnt(px, py, ant[i].carry);
    }

    SetRect(&hud, 0, 0, WIN_W, 14); EraseRect(&hud);
    MoveTo(6, 11);
    sprintf(buf, "Ant Farm    Day %ld    Ants %d    Food %ld%s",
            gday, nant, foodStore, paused ? "    [paused]" : "");
    DrawText(buf, 0, (short)strlen(buf));
}

/* ---- main -------------------------------------------------------------- */

int main(void)
{
    WindowPtr win;
    EventRecord ev;
    Rect r;
    long nextFrame;
    short sw, sh, winL, winT;
    unsigned char dirt[8] = {0xEE,0xBB,0xEE,0xBB,0xDD,0x77,0xDD,0x77};

    InitGraf(&qd.thePort);
    InitFonts(); InitWindows(); InitMenus(); TEInit(); InitDialogs(NULL); InitCursor();

    sw = qd.screenBits.bounds.right;
    sh = qd.screenBits.bounds.bottom;
    winL = (sw - WIN_W) / 2; if (winL < 4) winL = 4;
    winT = (sh - WIN_H) / 2 + 8; if (winT < 40) winT = 40;
    SetRect(&r, winL, winT, winL + WIN_W, winT + WIN_H);
    win = NewWindow(NULL, &r, "\pAnt Farm", true, documentProc, (WindowPtr)-1L, true, 0);
    SetPort(win);

    setPat(&patDirt, dirt);
    if (!makeOffscreen()) { SysBeep(20); return 1; }
    initFarm();

    nextFrame = TickCount();
    for (;;) {
        if (WaitNextEvent(everyEvent, &ev, 2, NULL)) {
            if (ev.what == keyDown || ev.what == autoKey) {
                char c = (char)(ev.message & charCodeMask);
                if ((ev.modifiers & cmdKey) && (c == 'q' || c == 'Q')) break;
                else if (c == ' ') paused = !paused;
                else if (c == 'r' || c == 'R') initFarm();
            } else if (ev.what == mouseDown) {
                WindowPtr wp;
                short part = FindWindow(ev.where, &wp);
                if (part == inGoAway && TrackGoAway(wp, ev.where)) break;
                else if (part == inDrag) DragWindow(wp, ev.where, &qd.screenBits.bounds);
                else if (part == inContent && wp == win) {
                    Point p = ev.where; SetPort(win); GlobalToLocal(&p);
                    addCrumb(p.h / CELL);
                }
            } else if (ev.what == updateEvt) {
                BeginUpdate((WindowPtr)ev.message); blit(win); EndUpdate((WindowPtr)ev.message);
            }
        }
        if (TickCount() >= nextFrame) {
            nextFrame = TickCount() + 4;
            if (!paused) stepSim();
            render();
            blit(win);
        }
    }

    if (gOffBits.baseAddr) DisposePtr(gOffBits.baseAddr);
    DisposeWindow(win);
    return 0;
}
