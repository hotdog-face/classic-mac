/*
 * mac_display.c -- 16-color index framebuffer -> 1-bit B&W at 2x (256x256),
 * blitted 1:1.  Real-hardware tuned (the Classic taught us its laws):
 *
 *   - NEVER let CopyBits scale: stretch blits are slow on the Classic.
 *     We dither straight to 2x and CopyBits 1:1.
 *   - THE DONUT PRINCIPLE for static rooms: the game's static background
 *     (room terrain) is pre-dithered ONCE per room into a 1-bit cache
 *     (mac_display_set_room).  Per frame, a dirty row is rebuilt by
 *     comparing each 4-pixel group (one long) against the room: equal ->
 *     copy the cached dithered byte; different (sprite pixels) -> dither
 *     just that byte.  Rows a sprite has left become pure cache copies.
 *     Per-frame dither work collapses to the few bytes sprites cover.
 *   - DIRTY-ROW tracking: unchanged p8_screen rows are neither rebuilt
 *     nor blitted; CopyBits moves only the changed band.
 */
#include "mac_display.h"
#include "p8.h"
#include "dither_tables.h"

#include <Quickdraw.h>
#include <Memory.h>
#include <string.h>

Rect gDisplayDstRect;

/* PHASE_DIAG split: last present's dither vs blit ticks + dirty-row count.
 * Zero-cost in release (never read). */
long mac_disp_dither_t, mac_disp_blit_t;
int  mac_disp_dirty_n, mac_disp_content_n;

#define OFF_W        DISP_W                 /* 256 */
#define OFF_H        DISP_H                 /* 256 */
#define OFF_ROWBYTES (OFF_W / 8)            /* 32  */
static BitMap    gOffBits;
static WindowPtr gWin;

static int gForceFull = 1;

/* solid-run patterns, [row-phase][color] = kPattern[color][row] */
static unsigned char gSolidPat[8][16];

/* the donut principle: static room indices + their pre-dithered rows */
static const unsigned char *gRoom;             /* 128x128 indices or NULL */
static unsigned char gRoomDith[P8_H][OFF_ROWBYTES];   /* 4KB, one 2x row per
                                                          src row (rows dup) */

/* dither 4 source pixels (one output byte) at row-phase `sprow` */
#define DITHER4(sprow, c0, c1, c2, c3) (unsigned char)\
    ( ((sprow)[(c0) & 15] & 0xC0) | ((sprow)[(c1) & 15] & 0x30) \
    | ((sprow)[(c2) & 15] & 0x0C) | ((sprow)[(c3) & 15] & 0x03) )

void mac_display_set_room(const unsigned char *room)
{
    gRoom = room;
    if (room) {
        int sy, bc;
        for (sy = 0; sy < P8_H; sy++) {
            const unsigned char *sprow = gSolidPat[sy & 7];
            const unsigned char *prow = room + sy * P8_W;
            unsigned char *o = gRoomDith[sy];
            for (bc = 0; bc < OFF_ROWBYTES; bc++) {
                int px0 = bc * 4;
                unsigned char c0 = prow[px0],   c1 = prow[px0+1];
                unsigned char c2 = prow[px0+2], c3 = prow[px0+3];
                if (c0 == c1 && c1 == c2 && c2 == c3)
                    o[bc] = sprow[c0 & 15];
                else
                    o[bc] = DITHER4(sprow, c0, c1, c2, c3);
            }
        }
    }
    gForceFull = 1;
}

OSErr mac_display_init(WindowPtr win)
{
    Ptr base;
    int c, r;

    gWin = win;
    base = NewPtrClear((Size)OFF_ROWBYTES * OFF_H);
    if (base == NULL) return MemError();

    gOffBits.baseAddr = base;
    gOffBits.rowBytes = OFF_ROWBYTES;
    SetRect(&gOffBits.bounds, 0, 0, OFF_W, OFF_H);

    for (c = 0; c < 16; c++)
        for (r = 0; r < 8; r++)
            gSolidPat[r][c] = kPattern[c][r];

    {
        Rect pr = win->portRect;
        short ox = (short)((pr.right - pr.left - DISP_W) / 2);
        short oy = (short)((pr.bottom - pr.top - DISP_H) / 2);
        if (ox < 0) ox = 0;
        if (oy < 0) oy = 0;
        SetRect(&gDisplayDstRect, ox, oy, ox + DISP_W, oy + DISP_H);
    }
    gForceFull = 1;
    return noErr;
}

void mac_display_dispose(void)
{
    if (gOffBits.baseAddr) { DisposePtr(gOffBits.baseAddr); gOffBits.baseAddr = NULL; }
}

void mac_display_invalidate(void)
{
    gForceFull = 1;
}

/* dither output bytes [bc0..bc1] of one 128-wide source row into `o`.
 * Each output byte covers 4 source pixels; the caller passes the byte range
 * that the row's dirty column span maps to (dirty-rectangle present). */
static void dither_row(int sy, unsigned char *o, int bc0, int bc1)
{
    const unsigned char *prow  = p8_screen + sy * P8_W;
    const unsigned char *sprow = gSolidPat[sy & 7];
    int bc;
    if (gRoom) {
        const long *pl = (const long *)prow;
        const long *rl = (const long *)(gRoom + sy * P8_W);
        const unsigned char *drow = gRoomDith[sy];
        for (bc = bc0; bc <= bc1; bc++) {
            if (pl[bc] == rl[bc]) o[bc] = drow[bc];
            else {
                int px0 = bc * 4;
                o[bc] = DITHER4(sprow, prow[px0], prow[px0+1],
                                       prow[px0+2], prow[px0+3]);
            }
        }
    } else {
        for (bc = bc0; bc <= bc1; bc++) {
            int px0 = bc * 4;
            unsigned char c0 = prow[px0], c1 = prow[px0+1];
            unsigned char c2 = prow[px0+2], c3 = prow[px0+3];
            if (c0 == c1 && c1 == c2 && c2 == c3) o[bc] = sprow[c0 & 15];
            else o[bc] = DITHER4(sprow, c0, c1, c2, c3);
        }
    }
}

/* the output-byte span [bx0..bx1] for source row sy's dirty column range, or
 * bx0>bx1 if the row is clean.  4 source px per output byte. */
#define ROW_SPAN(sy, bx0, bx1) do { \
        if (gForceFull) { (bx0) = 0; (bx1) = OFF_ROWBYTES - 1; } \
        else if (p8_dirty_x1[sy] < p8_dirty_x0[sy]) { (bx0) = 1; (bx1) = 0; } \
        else { (bx0) = p8_dirty_x0[sy] >> 2; (bx1) = p8_dirty_x1[sy] >> 2; } } while (0)

/* THE CLEVER PART: the frame budget was NOT dither or logic -- it was
 * CopyBits.  QuickDraw's blit path (mode dispatch, region setup, per-call
 * overhead) cost ~100ms/frame even for a small band.  So we skip it: the
 * game window is a fixed byte-aligned plainDBox, its port's portBits point
 * straight at screen memory, and a dithered 32-byte row is BlockMove'd
 * directly to the two screen rows it maps to.  No QuickDraw in the hot
 * path at all.  (Falls back to CopyBits if the window ever lands off a
 * byte boundary -- it can't move, but belt and suspenders.) */
void mac_display_present(void)
{
    GrafPtr port = (GrafPtr)gWin;
    long    srb  = (long)(port->portBits.rowBytes & 0x3FFF);   /* screen rB */
    long    xpix = (long)(gDisplayDstRect.left - port->portBits.bounds.left);
    int     sy;
    Ptr     scr0;

    if (xpix & 7) {                 /* not byte-aligned: QuickDraw fallback */
        int y;
        Rect srcR, dstR;
        for (sy = 0; sy < P8_H; sy++) {
            unsigned char *out0, *out1;
            if (!gForceFull && !p8_dirty_row[sy]) continue;
            out0 = (unsigned char *)(gOffBits.baseAddr + (sy*2)*OFF_ROWBYTES);
            out1 = out0 + OFF_ROWBYTES;
            dither_row(sy, out0, 0, OFF_ROWBYTES - 1);
            BlockMove(out0, out1, OFF_ROWBYTES);
        }
        SetPort(port);
        for (y = 0; y < P8_H; ) {
            if (gForceFull || p8_dirty_row[y]) {
                int y1 = y;
                while (y1 + 1 < P8_H && (gForceFull || p8_dirty_row[y1+1])) y1++;
                SetRect(&srcR, 0, (short)(y*2), OFF_W, (short)(y1*2+2));
                dstR = srcR;
                OffsetRect(&dstR, gDisplayDstRect.left, gDisplayDstRect.top);
                CopyBits(&gOffBits, &port->portBits, &srcR, &dstR, srcCopy, NULL);
                y = y1 + 1;
            } else y++;
        }
        gForceFull = 0;
        return;
    }

    /* fast path: dither straight to a stack row, blast both 2x rows to
     * screen RAM.  One dither + two BlockMoves per changed source row. */
    scr0 = port->portBits.baseAddr
         + (long)(gDisplayDstRect.top - port->portBits.bounds.top) * srb
         + xpix / 8;
#if PHASE_DIAG
    /* two passes so each is many rows (TickCount is 1/60s; a single row is
     * unmeasurable).  Pass 1 dithers into gOffBits, pass 2 blits.  mac_main
     * accumulates the two globals per second. */
    {
        unsigned long t0 = TickCount();
        int n = 0;
        for (sy = 0; sy < P8_H; sy++) {
            unsigned char *o; int bx0, bx1;
            if (!gForceFull && !p8_dirty_row[sy]) continue;
            ROW_SPAN(sy, bx0, bx1);
            if (bx0 > bx1) continue;
            o = (unsigned char *)(gOffBits.baseAddr + (sy*2)*OFF_ROWBYTES);
            dither_row(sy, o, bx0, bx1);
            n++;
        }
        mac_disp_dither_t = (long)(TickCount() - t0);
        mac_disp_dirty_n = n;
        { int cn = 0; for (sy = 0; sy < P8_H; sy++) cn += p8_content_row[sy];
          mac_disp_content_n = cn; }
        t0 = TickCount();
        for (sy = 0; sy < P8_H; sy++) {
            unsigned char *o; Ptr d; int bx0, bx1, nb;
            if (!gForceFull && !p8_dirty_row[sy]) continue;
            ROW_SPAN(sy, bx0, bx1);
            if (bx0 > bx1) continue;
            nb = bx1 - bx0 + 1;
            o = (unsigned char *)(gOffBits.baseAddr + (sy*2)*OFF_ROWBYTES);
            d = scr0 + (long)(sy * 2) * srb + bx0;
            BlockMove(o + bx0, d, nb);
            BlockMove(o + bx0, d + srb, nb);
        }
        mac_disp_blit_t = (long)(TickCount() - t0);
    }
    gForceFull = 0;
    return;
#endif
    for (sy = 0; sy < P8_H; sy++) {
        unsigned char row[OFF_ROWBYTES];
        Ptr d; int bx0, bx1, nb;
        if (!gForceFull && !p8_dirty_row[sy]) continue;
        ROW_SPAN(sy, bx0, bx1);            /* dither+blit only the dirty byte span */
        if (bx0 > bx1) continue;
        nb = bx1 - bx0 + 1;
        dither_row(sy, row, bx0, bx1);
        d = scr0 + (long)(sy * 2) * srb + bx0;
        BlockMove(row + bx0, d, nb);
        BlockMove(row + bx0, d + srb, nb);
    }
    gForceFull = 0;
}
