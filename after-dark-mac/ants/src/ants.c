/*
 * ants.c -- an After Dark graphics module: ants eat the desktop.
 *
 * A swarm of ants wanders in from the screen edges and gnaws the live
 * desktop image away, leaving ragged black trails until nothing is left
 * (at which point the ants keep scurrying as white specks -- a blanked
 * screen with signs of life).  The trick, learned from FadeAway/Hard
 * Rain on the same disk: the Blank message simply DOESN'T clear the
 * screen, so DrawFrame gets to chew on whatever you were working on.
 *
 * Interface (After Dark 1.0+): the 'ADgm' code resource is entered at
 * offset 0 with pascal convention:
 *     pascal OSErr main(Handle *storage, RgnHandle blankRgn,
 *                       short message, GMParamBlockPtr params);
 * messages: Initialize=0 Close=1 Blank=2 DrawFrame=3.  We deliberately
 * touch NOTHING in params (its layout grew between AD versions and we
 * run on 1.0): screen bounds come from blankRgn, patterns from
 * PenNormal, state lives in the storage Handle.  Zero writable globals.
 */
#include <MacTypes.h>
#include <Quickdraw.h>
#include <MacMemory.h>
#include <Events.h>
#include "Retro68Runtime.h"

enum { msgInitialize = 0, msgClose = 1, msgBlank = 2, msgDrawFrame = 3 };
#define kModuleError (-1)

#define ANTS     24
#define SUBSTEPS 3          /* eat+move cycles per ant per DrawFrame */
#define STEP     2          /* pixels per move */

typedef struct { short x, y; unsigned char dir, pad; } Ant;
typedef struct {
    Rect          b;        /* usable screen bounds */
    unsigned long seed;
    Ant           a[ANTS];
} State;

static const short kDX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const short kDY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static unsigned short rnd(State *s)
{
    s->seed = s->seed * 1103515245UL + 12345UL;
    return (unsigned short)((s->seed >> 16) & 0x7FFF);
}

static void spawn(State *s, Ant *a)
{
    short w = s->b.right - s->b.left, h = s->b.bottom - s->b.top;
    switch (rnd(s) & 3) {                     /* crawl in from an edge */
    case 0: a->x = s->b.left  + rnd(s) % w; a->y = s->b.top;        a->dir = 4; break;
    case 1: a->x = s->b.left  + rnd(s) % w; a->y = s->b.bottom - 1; a->dir = 0; break;
    case 2: a->x = s->b.left;               a->y = s->b.top + rnd(s) % h; a->dir = 2; break;
    default:a->x = s->b.right - 1;          a->y = s->b.top + rnd(s) % h; a->dir = 6; break;
    }
}

static void ants_frame(State *s)
{
    short i, k;
    Rect  r;
    PenNormal();                              /* black pen, patCopy */
    for (i = 0; i < ANTS; i++) {
        Ant *a = &s->a[i];
        for (k = 0; k < SUBSTEPS; k++) {
            unsigned short t;
            short sz = 2 + (short)(rnd(s) % 3);       /* ragged 4..8px bite */
            SetRect(&r, a->x - sz, a->y - sz, a->x + sz, a->y + sz);
            PaintRect(&r);                    /* eat (also covers old head) */

            t = rnd(s);
            if ((t & 7) == 0)      a->dir = (unsigned char)((a->dir + 1) & 7);
            else if ((t & 7) == 1) a->dir = (unsigned char)((a->dir + 7) & 7);
            else if ((t & 63) == 2) {
                /* hunger: if the path ahead is already eaten, turn hard */
                if (GetPixel(a->x + kDX[a->dir] * 6, a->y + kDY[a->dir] * 6))
                    a->dir = (unsigned char)(rnd(s) & 7);
            }
            a->x += kDX[a->dir] * STEP;
            a->y += kDY[a->dir] * STEP;
            if (a->x < s->b.left || a->x >= s->b.right ||
                a->y < s->b.top  || a->y >= s->b.bottom) {
                if (a->x < s->b.left)   a->x = s->b.left;
                if (a->x >= s->b.right) a->x = s->b.right - 1;
                if (a->y < s->b.top)    a->y = s->b.top;
                if (a->y >= s->b.bottom) a->y = s->b.bottom - 1;
                a->dir = (unsigned char)(rnd(s) & 7);
            }
        }
        SetRect(&r, a->x - 1, a->y - 1, a->x + 1, a->y + 1);
        InvertRect(&r);                       /* the ant itself: visible on
                                                 desktop AND on eaten black */
    }
}

/* pascal convention uppercases the symbol; pin the asm name so the
 * linker sees the real _start entry point (must sit at offset 0) */
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
        short  i;
        if (h == NULL) return kModuleError;
        *storage = h;
        s = (State *)*h;
        s->b = (**blankRgn).rgnBBox;
        InsetRect(&s->b, 2, 2);
        s->seed = (unsigned long)TickCount() | 1;
        for (i = 0; i < ANTS; i++) spawn(s, &s->a[i]);
        break;
    }
    case msgBlank:
        break;                                /* keep the desktop: dinner */
    case msgDrawFrame:
        if (*storage) {
            HLock(*storage);
            ants_frame((State *)**storage);
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
