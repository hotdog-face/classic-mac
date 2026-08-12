/*
 * show.c -- "Claude Show": a rotating multi-effect After Dark module.
 *
 * After Dark 1.0 has no Randomizer, so the rotation lives INSIDE one
 * module: every 15 minutes DrawFrame advances to the next act --
 *
 *   act 0  ANTS       eat whatever is on screen (the live desktop on
 *                      first activation; later, the previous act's art)
 *   act 1  STARFIELD  procedural stars accelerating out of the center
 *   act 2  PLASMA     slow-morphing blocky plasma from 8 dither levels
 *
 * ...then back to the ants, forever.  Same ABI/rules as ants.c: pascal
 * entry pinned to _start, zero writable globals, all state in the
 * storage Handle, nothing read from params (AD 1.0 layout differs from
 * the documented 2.0 header), bounds from blankRgn.
 */
#include <MacTypes.h>
#include <Quickdraw.h>
#include <MacMemory.h>
#include <Events.h>
#include "Retro68Runtime.h"

enum { msgInitialize = 0, msgClose = 1, msgBlank = 2, msgDrawFrame = 3 };
#define kModuleError (-1)

#ifndef ROTATE_TICKS
#define ROTATE_TICKS 54000L     /* 15 minutes of 60Hz ticks */
#endif
#define N_ACTS   3

/* ---- act 0: ants ---- */
#define ANTS     24
#define SUBSTEPS 3
#define STEP     2
typedef struct { short x, y; unsigned char dir, pad; } Ant;

/* ---- act 1: starfield ---- */
#define STARS    72
typedef struct { long x, y, vx, vy; } Star;   /* 24.8 fixed point */

/* ---- act 2: plasma ---- */
#define BLOCK    32
#define PCOLS    16              /* 512/32 */
#define PROWS    11              /* ceil(342/32) */

typedef struct {
    Rect          b;
    unsigned long seed;
    short         act;
    short         first;         /* first frame of this act */
    unsigned long actStart;      /* TickCount at act entry */
    Ant           a[ANTS];
    Star          s[STARS];
    short         t;                       /* plasma clock */
    unsigned char cell[PROWS][PCOLS];      /* last painted level, 0xFF=dirty */
} State;

static const short kDX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const short kDY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

/* 8 density patterns, white -> black (Bayer-ish) */
static const Pattern kLevel[8] = {
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {{0x88,0x00,0x00,0x00,0x88,0x00,0x00,0x00}},
    {{0x88,0x00,0x22,0x00,0x88,0x00,0x22,0x00}},
    {{0xAA,0x00,0xAA,0x00,0xAA,0x00,0xAA,0x00}},
    {{0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55}},
    {{0xAA,0xDD,0xAA,0x77,0xAA,0xDD,0xAA,0x77}},
    {{0xEE,0xFF,0xBB,0xFF,0xEE,0xFF,0xBB,0xFF}},
    {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}},
};
static const Pattern kBlackPat =
    {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};
static const Pattern kWhitePat =
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}};

/* 64-entry sine, -127..127 (quarter-symmetry filled at init would need
 * writable globals -- a small const table is simpler) */
static const signed char kSin[64] = {
      0,  12,  24,  36,  48,  59,  70,  80,  89,  98, 105, 112, 117, 121,
    124, 126, 127, 126, 124, 121, 117, 112, 105,  98,  89,  80,  70,  59,
     48,  36,  24,  12,   0, -12, -24, -36, -48, -59, -70, -80, -89, -98,
   -105,-112,-117,-121,-124,-126,-127,-126,-124,-121,-117,-112,-105, -98,
    -89, -80, -70, -59, -48, -36, -24, -12
};
#define SIN(i) kSin[(i) & 63]

static unsigned short rnd(State *s)
{
    s->seed = s->seed * 1103515245UL + 12345UL;
    return (unsigned short)((s->seed >> 16) & 0x7FFF);
}

/* ================= ants ================= */

static void ant_spawn(State *s, Ant *a)
{
    short w = s->b.right - s->b.left, h = s->b.bottom - s->b.top;
    switch (rnd(s) & 3) {
    case 0: a->x = s->b.left + rnd(s) % w; a->y = s->b.top;        a->dir = 4; break;
    case 1: a->x = s->b.left + rnd(s) % w; a->y = s->b.bottom - 1; a->dir = 0; break;
    case 2: a->x = s->b.left;              a->y = s->b.top + rnd(s) % h; a->dir = 2; break;
    default:a->x = s->b.right - 1;         a->y = s->b.top + rnd(s) % h; a->dir = 6; break;
    }
}

static void ants_enter(State *s)
{
    short i;                     /* no screen clear: dinner is served */
    for (i = 0; i < ANTS; i++) ant_spawn(s, &s->a[i]);
}

static void ants_frame(State *s)
{
    short i, k;
    Rect  r;
    PenNormal();
    for (i = 0; i < ANTS; i++) {
        Ant *a = &s->a[i];
        for (k = 0; k < SUBSTEPS; k++) {
            unsigned short t;
            short sz = 2 + (short)(rnd(s) % 3);
            SetRect(&r, a->x - sz, a->y - sz, a->x + sz, a->y + sz);
            PaintRect(&r);
            t = rnd(s);
            if ((t & 7) == 0)      a->dir = (unsigned char)((a->dir + 1) & 7);
            else if ((t & 7) == 1) a->dir = (unsigned char)((a->dir + 7) & 7);
            else if ((t & 63) == 2) {
                if (GetPixel(a->x + kDX[a->dir] * 6, a->y + kDY[a->dir] * 6))
                    a->dir = (unsigned char)(rnd(s) & 7);
            }
            a->x += kDX[a->dir] * STEP;
            a->y += kDY[a->dir] * STEP;
            if (a->x < s->b.left || a->x >= s->b.right ||
                a->y < s->b.top  || a->y >= s->b.bottom) {
                if (a->x < s->b.left)    a->x = s->b.left;
                if (a->x >= s->b.right)  a->x = s->b.right - 1;
                if (a->y < s->b.top)     a->y = s->b.top;
                if (a->y >= s->b.bottom) a->y = s->b.bottom - 1;
                a->dir = (unsigned char)(rnd(s) & 7);
            }
        }
        SetRect(&r, a->x - 1, a->y - 1, a->x + 1, a->y + 1);
        InvertRect(&r);
    }
}

/* ================= starfield ================= */

static void star_spawn(State *s, Star *st)
{
    /* random small velocity out of the center; position at center */
    short cx = (short)((s->b.left + s->b.right) / 2);
    short cy = (short)((s->b.top + s->b.bottom) / 2);
    short a = (short)(rnd(s) & 63);
    short sp = (short)(1 + (rnd(s) % 24));
    st->x = (long)cx << 8;
    st->y = (long)cy << 8;
    st->vx = (long)SIN(a + 16) * sp / 16;      /* cos */
    st->vy = (long)SIN(a) * sp / 16;
}

static void stars_enter(State *s)
{
    short i;
    Rect r = s->b;
    FillRect(&r, &kBlackPat);                  /* space is black */
    for (i = 0; i < STARS; i++) {
        star_spawn(s, &s->s[i]);
        /* scatter starting radii so they don't pulse in rings */
        s->s[i].x += s->s[i].vx * (short)(rnd(s) % 60);
        s->s[i].y += s->s[i].vy * (short)(rnd(s) % 60);
    }
}

static void stars_frame(State *s)
{
    short i;
    Rect  r;
    for (i = 0; i < STARS; i++) {
        Star *st = &s->s[i];
        short px = (short)(st->x >> 8), py = (short)(st->y >> 8);
        short sz;
        /* erase old */
        SetRect(&r, px, py, px + 2, py + 2);
        FillRect(&r, &kBlackPat);
        /* accelerate outward, move */
        st->vx += st->vx >> 5;
        st->vy += st->vy >> 5;
        st->x += st->vx;
        st->y += st->vy;
        px = (short)(st->x >> 8); py = (short)(st->y >> 8);
        if (px < s->b.left || px >= s->b.right - 2 ||
            py < s->b.top  || py >= s->b.bottom - 2) {
            star_spawn(s, st);
            px = (short)(st->x >> 8); py = (short)(st->y >> 8);
        }
        /* bigger when faster (closer) */
        sz = (st->vx > 512 || st->vx < -512 || st->vy > 512 || st->vy < -512)
             ? 2 : 1;
        SetRect(&r, px, py, px + sz, py + sz);
        FillRect(&r, &kWhitePat);
    }
}

/* ================= plasma ================= */

static void plasma_enter(State *s)
{
    Rect r = s->b;
    FillRect(&r, &kBlackPat);
    { short y, x;
      for (y = 0; y < PROWS; y++)
          for (x = 0; x < PCOLS; x++) s->cell[y][x] = 0xFF; }
    s->t = 0;
}

static void plasma_frame(State *s)
{
    short x, y;
    Rect  r;
    short t = s->t;
    for (y = 0; y < PROWS; y++) {
        for (x = 0; x < PCOLS; x++) {
            short v = (short)SIN(x * 5 + t) + (short)SIN(y * 7 - t)
                    + (short)SIN((x + y) * 3 + (t >> 1));
            unsigned char lvl = (unsigned char)((v + 381) * 8 / 763);
            if (lvl > 7) lvl = 7;
            if (lvl != s->cell[y][x]) {
                s->cell[y][x] = lvl;
                SetRect(&r, (short)(s->b.left + x * BLOCK),
                            (short)(s->b.top + y * BLOCK),
                            (short)(s->b.left + (x + 1) * BLOCK),
                            (short)(s->b.top + (y + 1) * BLOCK));
                if (r.right > s->b.right)   r.right = s->b.right;
                if (r.bottom > s->b.bottom) r.bottom = s->b.bottom;
                FillRect(&r, &kLevel[lvl]);
            }
        }
    }
    s->t = (short)(t + 1);
}

/* ================= dispatcher ================= */

static void act_enter(State *s)
{
    s->first = 0;
    s->actStart = TickCount();
    switch (s->act) {
    case 0: ants_enter(s);   break;
    case 1: stars_enter(s);  break;
    default: plasma_enter(s); break;
    }
}

pascal OSErr module_main(Handle *storage, RgnHandle blankRgn,
                         short message, void *params) __asm__("_start");

pascal OSErr module_main(Handle *storage, RgnHandle blankRgn,
                         short message, void *params)
{
    RETRO68_RELOCATE();
    (void)params;

    switch (message) {
    case msgInitialize: {
        Handle h = NewHandle(sizeof(State));
        State *s;
        if (h == NULL) return kModuleError;
        *storage = h;
        s = (State *)*h;
        s->b = (**blankRgn).rgnBBox;
        InsetRect(&s->b, 2, 2);
        s->seed = (unsigned long)TickCount() | 1;
        s->act = 0;
        s->first = 1;            /* act_enter on first DrawFrame */
        break;
    }
    case msgBlank:
        break;                   /* act 0 eats the live desktop */
    case msgDrawFrame:
        if (*storage) {
            State *s;
            HLock(*storage);
            s = (State *)**storage;
            if (s->first) act_enter(s);
            else if (TickCount() - s->actStart >= (unsigned long)ROTATE_TICKS) {
                s->act = (short)((s->act + 1) % N_ACTS);
                act_enter(s);
            }
            switch (s->act) {
            case 0: ants_frame(s);   break;
            case 1: stars_frame(s);  break;
            default: plasma_frame(s); break;
            }
            HUnlock(*storage);
        }
        break;
    case msgClose:
        if (*storage) { DisposeHandle(*storage); *storage = NULL; }
        Retro68FreeGlobals();
        break;
    default:
        break;
    }
    return noErr;
}
