/*
 * mac_display.c -- 16-color index framebuffer -> 1-bit B&W, blitted to the
 * window.  Tuned for the 8MHz 68000 (see PORTING-PLAN.md §3, §7):
 *
 *   - We render the 256x256 output directly and CopyBits 1:1 (the FAST QuickDraw
 *     path).  Stretching CopyBits 128->256 turned out to be a major cost.
 *   - The dither has a SOLID-RUN fast path: an output byte covering 4 source
 *     pixels of the same color is looked up from a precomputed pattern table in
 *     one shot instead of thresholding 8 pixels.  Most of the screen is flat
 *     color (background, fills, curtains), so this path dominates.
 *   - Each source row is dithered once and the output row is duplicated (the 2x
 *     vertical scale), halving the per-row threshold work.
 *
 * The dither threshold is still keyed to screen coordinates so flat fills render
 * as stable, non-crawling patterns and runtime pal() swaps composite correctly.
 */
#include "mac_display.h"
#include "p8.h"

#include <Quickdraw.h>
#include <Memory.h>
#include <string.h>

/* set when the window needs a full repaint (update events, first frame) */
static int gForceFull = 1;

Rect gDisplayDstRect;

/* full-res 256x256 1-bit offscreen, rowBytes = 32 */
#define OFF_W        DISP_W                 /* 256 -- dither straight to 2x; */
#define OFF_H        DISP_H                 /* 256    CopyBits blits 1:1      */
#define OFF_ROWBYTES (OFF_W / 8)            /* 32  (stretch blits are SLOW)   */
static BitMap    gOffBits;
static WindowPtr gWin;

/* "blackness" 0..64 per PICO-8 color.  Deliberately BIMODAL: mid-luminance
 * colors are pushed toward black or white so large fills read as clean solids
 * with sparse texture instead of muddy 50% checker (owner feedback: outlines
 * over dithered fills).  Only colors 9-15 keep a light stipple. */
static const unsigned char kBlackness[16] = {
    64, 58, 50, 46, 44, 50,  4,  0,
    54, 14,  6, 16, 18, 20, 22,  8
};

/* classic Bayer 8x8, values 0..63 */
static const unsigned char kBayer8[8][8] = {
    {  0,32, 8,40, 2,34,10,42},{ 48,16,56,24,50,18,58,26},
    { 12,44, 4,36,14,46, 6,38},{ 60,28,52,20,62,30,54,22},
    {  3,35,11,43, 1,33, 9,41},{ 51,19,59,27,49,17,57,25},
    { 15,47, 7,39,13,45, 5,37},{ 63,31,55,23,61,29,53,21}
};

/* solid-run patterns, indexed [row-phase][color]: the 8-bit output for 8
 * consecutive output columns of one color (bit 7 = leftmost). Because the Bayer
 * x-period is 8 == one output byte, this is independent of column.  Row-major by
 * phase so the hot loop holds a single `sprow = gSolidPat[sy&7]` row pointer. */
static unsigned char gSolidPat[8][16];

/* the donut principle: static background indices + pre-dithered 2x rows */
static const unsigned char *gRoom;                    /* 128x128 or NULL */
static unsigned char gRoomDith[P8_H][OFF_ROWBYTES];   /* 4KB cache */

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
    int c, r, oc;

    gWin = win;
    base = NewPtrClear((Size)OFF_ROWBYTES * OFF_H);
    if (base == NULL) return MemError();

    gOffBits.baseAddr = base;
    gOffBits.rowBytes = OFF_ROWBYTES;
    SetRect(&gOffBits.bounds, 0, 0, OFF_W, OFF_H);

    for (c = 0; c < 16; c++)
        for (r = 0; r < 8; r++) {
            unsigned char b = 0;
            for (oc = 0; oc < 8; oc++)
                if (kBlackness[c] > kBayer8[r][oc]) b |= (unsigned char)(1 << (7 - oc));
            gSolidPat[r][c] = b;
        }

    {
        Rect pr = win->portRect;
        short ox = (short)((pr.right - pr.left - DISP_W) / 2);
        short oy = (short)((pr.bottom - pr.top - DISP_H) / 2);
        if (ox < 0) ox = 0;
        if (oy < 0) oy = 0;
        SetRect(&gDisplayDstRect, ox, oy, ox + DISP_W, oy + DISP_H);
    }
    return noErr;
}

void mac_display_dispose(void)
{
    if (gOffBits.baseAddr) { DisposePtr(gOffBits.baseAddr); gOffBits.baseAddr = NULL; }
}

void mac_display_invalidate(void) { gForceFull = 1; }

/* dither output bytes [bc0..bc1] of source row sy into `o` (the donut fast-path
 * copies cached background bytes; only sprite bytes are freshly dithered) */
static void dither_row(int sy, unsigned char *o, int bc0, int bc1)
{
    const unsigned char *sprow = gSolidPat[sy & 7];
    const unsigned char *prow  = p8_screen + sy * P8_W;
    int bc;
    if (gRoom) {
        const long *pl = (const long *)prow;
        const long *rl = (const long *)(gRoom + sy * P8_W);
        const unsigned char *drow = gRoomDith[sy];
        for (bc = bc0; bc <= bc1; bc++) {
            if (pl[bc] == rl[bc]) o[bc] = drow[bc];
            else { int px0 = bc * 4;
                o[bc] = DITHER4(sprow, prow[px0], prow[px0+1], prow[px0+2], prow[px0+3]); }
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

/* the output-byte span for source row sy's dirty column range (4 src px/byte) */
#define ROW_SPAN(sy, bx0, bx1) do { \
        if (gForceFull) { (bx0) = 0; (bx1) = OFF_ROWBYTES - 1; } \
        else if (p8_dirty_x1[sy] < p8_dirty_x0[sy]) { (bx0) = 1; (bx1) = 0; } \
        else { (bx0) = p8_dirty_x0[sy] >> 2; (bx1) = p8_dirty_x1[sy] >> 2; } } while (0)

void mac_display_present(void)
{
    GrafPtr port = (GrafPtr)gWin;
    long    srb  = (long)(port->portBits.rowBytes & 0x3FFF);
    long    xpix = (long)(gDisplayDstRect.left - port->portBits.bounds.left);
    int     sy;
    Ptr     scr0;

    if (xpix & 7) {                 /* not byte-aligned: dither full rows + CopyBits band */
        int ymin = P8_H, ymax = -1;
        Ptr base = gOffBits.baseAddr;
        for (sy = 0; sy < P8_H; sy++) {
            unsigned char *out0, *out1; int k;
            if (!gForceFull && !p8_touched[sy] && !p8_restored[sy]) continue;
            if (sy < ymin) ymin = sy;
            if (sy > ymax) ymax = sy;
            out0 = (unsigned char *)(base + (sy * 2) * OFF_ROWBYTES);
            out1 = out0 + OFF_ROWBYTES;
            dither_row(sy, out0, 0, OFF_ROWBYTES - 1);
            for (k = 0; k < OFF_ROWBYTES; k++) out1[k] = out0[k];
        }
        if (gForceFull) { gForceFull = 0; ymin = 0; ymax = P8_H - 1; }
        if (ymax < ymin) return;
        { Rect srcR, dstR;
          SetRect(&srcR, 0, (short)(ymin*2), OFF_W, (short)(ymax*2+2));
          dstR = srcR; OffsetRect(&dstR, gDisplayDstRect.left, gDisplayDstRect.top);
          SetPort(port);
          CopyBits(&gOffBits, &port->portBits, &srcR, &dstR, srcCopy, NULL); }
        return;
    }

    /* fast path: dither the dirty byte-span to a stack row, BlockMove both 2x
     * rows straight to screen RAM -- no CopyBits, no full-row work. */
    scr0 = port->portBits.baseAddr
         + (long)(gDisplayDstRect.top - port->portBits.bounds.top) * srb
         + xpix / 8;
    for (sy = 0; sy < P8_H; sy++) {
        unsigned char row[OFF_ROWBYTES];
        Ptr d; int bx0, bx1, nb;
        if (!gForceFull && !p8_touched[sy] && !p8_restored[sy]) continue;
        ROW_SPAN(sy, bx0, bx1);
        if (bx0 > bx1) continue;
        nb = bx1 - bx0 + 1;
        dither_row(sy, row, bx0, bx1);
        d = scr0 + (long)(sy * 2) * srb + bx0;
        BlockMove(row + bx0, d, nb);
        BlockMove(row + bx0, d + srb, nb);
    }
    gForceFull = 0;
}
