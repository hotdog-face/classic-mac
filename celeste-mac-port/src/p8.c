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

#ifdef PX_COUNT
unsigned long g_px_sprite, g_px_restore, g_px_fill;   /* pixel-write profiler */
#define PXADD(v, n) ((v) += (unsigned long)(n))
#else
#define PXADD(v, n) ((void)0)
#endif

/* ---- dirty rows: TWO row-flag sets, the key to a converging frame.
 *
 *   p8_dirty_row[]   RENDER set: rows whose pixels changed this frame, so
 *                    mac_display must re-dither+blit them.  Marked by EVERY
 *                    write (the room restore AND sprite draws).
 *   p8_content_row[] ERASE set: rows that carry non-room content (sprites,
 *                    overlays) that must be restored to room next frame.
 *                    Marked ONLY by content draws, NOT by the room restore.
 *
 * The bug this fixes: with one set, the frame-start restore re-marked
 * exactly the rows it restored, so the dirty set never shrank below the
 * room-entry full redraw (R=128 forever).  Split, it converges: each
 * frame the glue erases last frame's CONTENT rows, draws this frame's,
 * and RENDER = (erased rows) union (new content rows) ~ 2x the sprites,
 * not the whole screen. */
unsigned char p8_dirty_row[P8_H];       /* render: set all in p8_init */
unsigned char p8_content_row[P8_H];     /* erase */

/* ---- per-row changed-COLUMN spans (the dirty-rectangle optimization) ----
 * For each row we also track the min/max source column touched, so the restore,
 * dither and blit process only the ~8-px-wide band a sprite occupies instead of
 * the full 128-px row.  Units: source pixels (0..127), inclusive; x0 > x1 means
 * the row's span is empty.  _dirty_ is this frame's RENDER span (present reads
 * it); _content_ is this frame's CONTENT span (saved for next frame's erase). */
short p8_dirty_x0[P8_H], p8_dirty_x1[P8_H];
short p8_content_x0[P8_H], p8_content_x1[P8_H];

void p8_dirty_reset(void)
{
    int r;
    memset(p8_dirty_row, 0, P8_H);
    for (r = 0; r < P8_H; r++) { p8_dirty_x0[r] = P8_W; p8_dirty_x1[r] = -1; }
}
void p8_content_reset(void)
{
    int r;
    memset(p8_content_row, 0, P8_H);
    for (r = 0; r < P8_H; r++) { p8_content_x0[r] = P8_W; p8_content_x1[r] = -1; }
}

/* expand row y's render span to cover columns [xa..xb] (clamped to screen) */
#define SPAN_R(y, xa, xb) do { \
        short _a = (xa), _b = (xb); \
        if (_a < 0) _a = 0; if (_b > P8_W - 1) _b = P8_W - 1; \
        if (_a <= _b) { \
            if (_a < p8_dirty_x0[y]) p8_dirty_x0[y] = _a; \
            if (_b > p8_dirty_x1[y]) p8_dirty_x1[y] = _b; } } while (0)
/* room restore etc.: render only (rows changed, x-span given) */
#define MARK_ROWS_X(a, b, xa, xb) do { int _r; \
        for (_r = (a); _r <= (b); _r++) { p8_dirty_row[_r] = 1; SPAN_R(_r, xa, xb); } } while (0)
#define MARK_ROWS(a, b) MARK_ROWS_X(a, b, 0, P8_W - 1)
/* sprite/primitive draws: render AND content (erase next frame), x-span given */
#define MARK_CONTENT_X(a, b, xa, xb) do { int _r; \
        for (_r = (a); _r <= (b); _r++) { \
            p8_dirty_row[_r] = 1; p8_content_row[_r] = 1; \
            SPAN_R(_r, xa, xb); \
            { short _a = (xa), _b = (xb); \
              if (_a < 0) _a = 0; if (_b > P8_W - 1) _b = P8_W - 1; \
              if (_a <= _b) { \
                  if (_a < p8_content_x0[_r]) p8_content_x0[_r] = _a; \
                  if (_b > p8_content_x1[_r]) p8_content_x1[_r] = _b; } } } } while (0)
#define MARK_CONTENT(a, b) MARK_CONTENT_X(a, b, 0, P8_W - 1)

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
    memset(p8_dirty_row, 1, P8_H);
    memset(p8_content_row, 1, P8_H);
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

int p8_pal_is_default(void)
{
    int i;
    for (i = 0; i < 16; i++)
        if (pal_map[i] != (unsigned char)i) return 0;
    return 1;
}

/* copy source rows [sy0..sy1] of a prerendered 128x128 index layer to the
 * screen: camera + clip only, no palette pass (callers guard with
 * p8_pal_is_default). */
void p8_blit_layer(const unsigned char *buf, int sentinel, int sy0, int sy1)
{
    int dy, dy0, dy1;
    if (sentinel < 0 && cam_x == 0 && cam_y == 0 &&
        clip_x0 == 0 && clip_y0 == 0 &&
        clip_x1 == P8_W - 1 && clip_y1 == P8_H - 1 &&
        sy0 <= 0 && sy1 >= P8_H - 1) {
        memcpy(p8_screen, buf, (unsigned)(P8_W * P8_H));   /* the hot path */
        PXADD(g_px_restore, P8_W * P8_H);
        MARK_ROWS(0, P8_H - 1);
        return;
    }
    dy0 = sy0 - cam_y; if (dy0 < clip_y0) dy0 = clip_y0;
    dy1 = sy1 - cam_y; if (dy1 > clip_y1) dy1 = clip_y1;
    for (dy = dy0; dy <= dy1; dy++) {
        int sy = dy + cam_y;
        const unsigned char *srow;
        unsigned char *drow;
        int dx0 = clip_x0, dx1 = clip_x1, sx0;
        if (sy < 0 || sy >= P8_H) continue;
        srow = buf + sy * P8_W;
        drow = p8_screen + dy * P8_W;
        sx0 = dx0 + cam_x;
        if (sx0 < 0)            { dx0 -= sx0; sx0 = 0; }
        if (sx0 + (dx1 - dx0) > P8_W - 1) dx1 = dx0 + (P8_W - 1 - sx0);
        if (dx1 < dx0) continue;
        MARK_ROWS_X(dy, dy, dx0, dx1);
        if (sentinel < 0) {
            int n = dx1 - dx0 + 1, k;
            PXADD(g_px_restore, n);
            for (k = 0; k < n; k++) drow[dx0 + k] = srow[sx0 + k];
        } else {
            int k;
            PXADD(g_px_restore, dx1 - dx0 + 1);
            for (k = 0; k <= dx1 - dx0; k++) {
                unsigned char v = srow[sx0 + k];
                if (v != (unsigned char)sentinel) drow[dx0 + k] = v;
            }
        }
    }
}

/* Restore one row's [x0..x1] source-pixel span from a layer buffer (the room),
 * marking exactly that render span.  The dirty-rectangle restore: the glue calls
 * this per content row using LAST frame's content span, so we copy back only the
 * ~8-px band a sprite sat on, not the whole 128-px row. */
void p8_restore_span(const unsigned char *buf, int y, int x0, int x1)
{
    if (y < 0 || y >= P8_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > P8_W - 1) x1 = P8_W - 1;
    if (x0 > x1) return;
    memcpy(p8_screen + y * P8_W + x0, buf + y * P8_W + x0, (unsigned)(x1 - x0 + 1));
    PXADD(g_px_restore, x1 - x0 + 1);
    p8_dirty_row[y] = 1;
    SPAN_R(y, x0, x1);
}

/* ---------- raw pixel (post-camera, clipped, palette-mapped) ---------- */
static void put_pixel(int x, int y, int c)
{
    x -= cam_x; y -= cam_y;
    if (x < clip_x0 || x > clip_x1 || y < clip_y0 || y > clip_y1) return;
    p8_screen[y * P8_W + x] = pal_map[c & 15];
    MARK_CONTENT_X(y, y, x, x);
}

/* ---------- lifecycle: clear ---------- */
void p8_cls(void)
{
    int i;
    for (i = 0; i < P8_W * P8_H; i++) p8_screen[i] = 0;
    MARK_CONTENT(0, P8_H - 1);
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
    MARK_CONTENT_X(y0, y1, x0, x1);
    for (y = y0; y <= y1; y++) {
        PXADD(g_px_fill, x1 - x0 + 1);
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
            MARK_CONTENT_X(scr_y, scr_y, base_dx + ix0, base_dx + ix1);
            PXADD(g_px_sprite, ix1 - ix0 + 1);
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
