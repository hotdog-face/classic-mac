/*
 * p8.c -- PICO-8 drawing API over a 128x128 palette-index framebuffer.
 * See p8.h and PORTING-PLAN.md §2.  No Mac Toolbox calls live here.
 */
#include "p8.h"
#include "p8_font.h"
#include "generated/gfx_data.h"
#include "generated/sprite_flags.h"
#include "generated/map_data.h"
#include <string.h>

unsigned char p8_screen[P8_W * P8_H];
unsigned char p8_touched[P8_H];
unsigned char p8_restored[P8_H];

/* ---- per-row changed-COLUMN spans (dirty-rectangle optimization) ----
 * p8_touched_x0/x1 : this frame's touched span per row (source px, inclusive,
 *                    x0>x1 = empty) -- saved for next frame's background restore.
 * p8_dirty_x0/x1   : this frame's render span (restored span U touched span) --
 *                    mac_display dithers+blits only this band, not the full row. */
short p8_touched_x0[P8_H], p8_touched_x1[P8_H];
short p8_dirty_x0[P8_H], p8_dirty_x1[P8_H];

void p8_spans_reset(void)     /* called by game_draw at the top of each frame */
{
    int r;
    for (r = 0; r < P8_H; r++) {
        p8_touched_x0[r] = P8_W; p8_touched_x1[r] = -1;
        p8_dirty_x0[r]   = P8_W; p8_dirty_x1[r]   = -1;
    }
}
/* mark row y touched over columns [xa..xb], expanding both spans (clamped) */
#define TOUCH_SPAN(y, xa, xb) do { \
        short _a = (xa), _b = (xb); \
        if (_a < 0) _a = 0; if (_b > P8_W - 1) _b = P8_W - 1; \
        p8_touched[y] = 1; \
        if (_a <= _b) { \
            if (_a < p8_touched_x0[y]) p8_touched_x0[y] = _a; \
            if (_b > p8_touched_x1[y]) p8_touched_x1[y] = _b; \
            if (_a < p8_dirty_x0[y])   p8_dirty_x0[y]   = _a; \
            if (_b > p8_dirty_x1[y])   p8_dirty_x1[y]   = _b; } } while (0)

/* restore one row's [x0..x1] span from the background and mark it dirty */
void p8_restore_span(const unsigned char *bg, int y, int x0, int x1)
{
    if (y < 0 || y >= P8_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > P8_W - 1) x1 = P8_W - 1;
    if (x0 > x1) return;
    memcpy(p8_screen + y * P8_W + x0, bg + y * P8_W + x0, (unsigned)(x1 - x0 + 1));
    p8_restored[y] = 1;
    if (x0 < p8_dirty_x0[y]) p8_dirty_x0[y] = (short)x0;
    if (x1 > p8_dirty_x1[y]) p8_dirty_x1[y] = (short)x1;
}

/* ---- draw state ---- */
static int cam_x, cam_y;                 /* camera offset (subtracted) */
static int clip_x0, clip_y0, clip_x1, clip_y1; /* inclusive clip rect */
static int draw_color = 6;
static unsigned char pal_map[16];        /* pal(): color remap for draws */
static unsigned char pal_transparent[16];/* palt(): 1 == treated as transparent */

/* ---------- lifecycle ---------- */
void p8_reset_draw_state(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        pal_map[i] = (unsigned char)i;
        pal_transparent[i] = 0;
    }
    pal_transparent[0] = 1;              /* color 0 transparent by default */
    cam_x = cam_y = 0;
    clip_x0 = 0; clip_y0 = 0; clip_x1 = P8_W - 1; clip_y1 = P8_H - 1;
}

void p8_init(void)
{
    p8_reset_draw_state();
    p8_cls();
}

/* ---------- draw state setters ---------- */
void p8_camera(int x, int y) { cam_x = x; cam_y = y; }
void p8_camera_reset(void)   { cam_x = 0; cam_y = 0; }

void p8_clip(int x, int y, int w, int h)
{
    clip_x0 = x; clip_y0 = y; clip_x1 = x + w - 1; clip_y1 = y + h - 1;
    if (clip_x0 < 0) clip_x0 = 0;
    if (clip_y0 < 0) clip_y0 = 0;
    if (clip_x1 > P8_W - 1) clip_x1 = P8_W - 1;
    if (clip_y1 > P8_H - 1) clip_y1 = P8_H - 1;
}
void p8_clip_reset(void)
{
    clip_x0 = 0; clip_y0 = 0; clip_x1 = P8_W - 1; clip_y1 = P8_H - 1;
}

void p8_color(int c) { draw_color = c & 15; }

void p8_pal(int c0, int c1) { pal_map[c0 & 15] = (unsigned char)(c1 & 15); }
void p8_pal_reset(void)
{
    int i;
    for (i = 0; i < 16; i++) pal_map[i] = (unsigned char)i;
    /* pal() also resets transparency to the default (only 0 transparent) */
    for (i = 0; i < 16; i++) pal_transparent[i] = 0;
    pal_transparent[0] = 1;
}
void p8_palt(int c, int transparent) { pal_transparent[c & 15] = transparent ? 1 : 0; }
void p8_palt_reset(void)
{
    int i;
    for (i = 0; i < 16; i++) pal_transparent[i] = 0;
    pal_transparent[0] = 1;
}

/* ---------- raw pixel (post-camera, clipped, palette-mapped) ---------- */
static void put_pixel(int x, int y, int c)
{
    x -= cam_x; y -= cam_y;
    if (x < clip_x0 || x > clip_x1 || y < clip_y0 || y > clip_y1) return;
    TOUCH_SPAN(y, x, x);
    p8_screen[y * P8_W + x] = pal_map[c & 15];
}

/* ---------- lifecycle: clear ---------- */
void p8_cls(void)
{
    int i;
    for (i = 0; i < P8_W * P8_H; i++) p8_screen[i] = 0;
    for (i = 0; i < P8_H; i++) {
        p8_touched[i] = 1;
        p8_touched_x0[i] = 0; p8_touched_x1[i] = P8_W - 1;   /* full-width clear */
        p8_dirty_x0[i]   = 0; p8_dirty_x1[i]   = P8_W - 1;
    }
}

/* ---------- primitives ---------- */
void p8_pset(int x, int y, int c) { put_pixel(x, y, c); }

int p8_pget(int x, int y)
{
    x -= cam_x; y -= cam_y;
    if (x < 0 || x >= P8_W || y < 0 || y >= P8_H) return 0;
    return p8_screen[y * P8_W + x];
}

void p8_rectfill(int x0, int y0, int x1, int y1, int c)
{
    int y, t;
    unsigned char m = pal_map[c & 15];
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    /* apply camera + clip once, then memset each row (no per-pixel overhead) */
    x0 -= cam_x; x1 -= cam_x; y0 -= cam_y; y1 -= cam_y;
    if (x0 < clip_x0) x0 = clip_x0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y0 < clip_y0) y0 = clip_y0;
    if (y1 > clip_y1) y1 = clip_y1;
    if (x1 < x0 || y1 < y0) return;
    for (y = y0; y <= y1; y++) {
        TOUCH_SPAN(y, x0, x1);
        memset(p8_screen + y * P8_W + x0, m, (size_t)(x1 - x0 + 1));
    }
}

void p8_rect(int x0, int y0, int x1, int y1, int c)
{
    int x, y, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    for (x = x0; x <= x1; x++) { put_pixel(x, y0, c); put_pixel(x, y1, c); }
    for (y = y0; y <= y1; y++) { put_pixel(x0, y, c); put_pixel(x1, y, c); }
}

/* Bresenham */
void p8_line(int x0, int y0, int x1, int y1, int c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, e2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    err = dx - dy;
    for (;;) {
        put_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void p8_circfill(int cx, int cy, int r, int c)
{
    int y, x;
    if (r < 0) return;
    for (y = -r; y <= r; y++) {
        for (x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r + r) /* +r fudge matches PICO-8's chunky circles */
                put_pixel(cx + x, cy + y, c);
        }
    }
}

/* midpoint circle, outline only (for line-art telegraphs) */
void p8_circ(int cx, int cy, int r, int c)
{
    int x = r, y = 0, err = 1 - r;
    if (r < 0) return;
    if (r == 0) { put_pixel(cx, cy, c); return; }
    while (x >= y) {
        put_pixel(cx + x, cy + y, c); put_pixel(cx - x, cy + y, c);
        put_pixel(cx + x, cy - y, c); put_pixel(cx - x, cy - y, c);
        put_pixel(cx + y, cy + x, c); put_pixel(cx - y, cy + x, c);
        put_pixel(cx + y, cy - x, c); put_pixel(cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

/* ---------- sprite sheet sampling ---------- */
static int sheet_pixel(int sx, int sy)
{
    if (sx < 0 || sx >= GFX_W || sy < 0 || sy >= GFX_H) return 0;
    return gfx_data[sy * GFX_W + sx];
}

void p8_sspr(int sx, int sy, int sw, int sh,
             int dx, int dy, int dw, int dh, int flip_x, int flip_y)
{
    int ix, iy;
    if (dw <= 0) dw = sw;
    if (dh <= 0) dh = sh;

    /* FAST PATH: 1:1 scale (every game sprite).  Hoist the camera + clip out of
     * the per-pixel loop and write straight to the framebuffer. */
    if (dw == sw && dh == sh) {
        for (iy = 0; iy < sh; iy++) {
            int ssy = flip_y ? (sh - 1 - iy) : iy;
            int scr_y = dy + iy - cam_y;
            const unsigned char *srow;
            unsigned char *drow;
            int ix0, ix1, base_dx;
            if (scr_y < clip_y0 || scr_y > clip_y1) continue;
            if (sy + ssy < 0 || sy + ssy >= GFX_H) continue;
            srow = gfx_data + (sy + ssy) * GFX_W;
            drow = p8_screen + scr_y * P8_W;
            base_dx = dx - cam_x;                    /* screen x of ix==0 */
            ix0 = 0; ix1 = sw - 1;                   /* clip x-range once */
            if (base_dx + ix0 < clip_x0) ix0 = clip_x0 - base_dx;
            if (base_dx + ix1 > clip_x1) ix1 = clip_x1 - base_dx;
            TOUCH_SPAN(scr_y, base_dx + ix0, base_dx + ix1);
            for (ix = ix0; ix <= ix1; ix++) {
                int ssx = flip_x ? (sw - 1 - ix) : ix;
                int spos = sx + ssx;
                unsigned char col;
                if (spos < 0 || spos >= GFX_W) continue;
                col = srow[spos];
                if (!pal_transparent[col]) drow[base_dx + ix] = pal_map[col];
            }
        }
        return;
    }

    /* general (scaled) path -- unused by the game today, kept for completeness */
    for (iy = 0; iy < dh; iy++) {
        int syy = sh == dh ? iy : (iy * sh) / dh;
        int ssy = flip_y ? (sh - 1 - syy) : syy;
        for (ix = 0; ix < dw; ix++) {
            int sxx = sw == dw ? ix : (ix * sw) / dw;
            int ssx = flip_x ? (sw - 1 - sxx) : sxx;
            int col = sheet_pixel(sx + ssx, sy + ssy);
            if (!pal_transparent[col])
                put_pixel(dx + ix, dy + iy, col);
        }
    }
}

void p8_spr(int n, int x, int y, int w, int h, int flip_x, int flip_y)
{
    int sx = (n % 16) * 8;
    int sy = (n / 16) * 8;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    p8_sspr(sx, sy, w * 8, h * 8, x, y, w * 8, h * 8, flip_x, flip_y);
}

/* ---------- map ---------- */
int p8_mget(int cx, int cy)
{
    if (cx < 0 || cx >= MAP_W || cy < 0 || cy >= MAP_H) return 0;
    return map_data[cy * MAP_W + cx];
}

void p8_map(int cell_x, int cell_y, int sx, int sy, int cw, int ch)
{
    int cy, cx;
    for (cy = 0; cy < ch; cy++) {
        for (cx = 0; cx < cw; cx++) {
            int n = p8_mget(cell_x + cx, cell_y + cy);
            if (n != 0)                    /* PICO-8 skips sprite 0 in map() */
                p8_spr(n, sx + cx * 8, sy + cy * 8, 1, 1, 0, 0);
        }
    }
}

/* ---------- flags ---------- */
int p8_fget(int n)
{
    if (n < 0 || n >= 256) return 0;
    return sprite_flags[n];
}
int p8_fget_bit(int n, int b) { return (p8_fget(n) >> (b & 7)) & 1; }

/* ---------- text ---------- */
int p8_print_width(const char *s)
{
    int w = 0;
    while (*s++) w += FONT_ADVANCE;
    return w;
}

void p8_print(const char *s, int x, int y, int c)
{
    int cx = x;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= FONT_FIRST && ch <= FONT_LAST) {
            const unsigned char *g = p8_font[ch - FONT_FIRST];
            int row, col;
            for (row = 0; row < FONT_ROWS; row++) {
                unsigned char bits = g[row];
                for (col = 0; col < 3; col++)
                    if (bits & (1 << (2 - col)))
                        put_pixel(cx + col, y + row, c);
            }
        }
        cx += FONT_ADVANCE;
    }
}
