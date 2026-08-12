/*
 * celeste_glue.c -- implements ccleste's 14-call PICO-8 callback interface
 * (celeste.h) on top of the p8.c index-framebuffer emulation layer reused
 * from the Just One Boss port.
 *
 * Semantics mirror ccleste's reference frontend (sdl12main.c pico8emu())
 * exactly, including the map-mask special case.  Camera handling differs in
 * mechanism but not behavior: p8.c applies the camera offset internally.
 *
 * THE ROOM CACHE (the 68000's frame budget lives here):
 * celeste re-tiles three full 16x16 map layers every frame -- unaffordable
 * at 7.8MHz.  Rooms are static, so the whole static part of the frame
 * (background fill + mask-4 decor layer + mask-2 solid terrain) is composed
 * into ONE index buffer per room and restored each frame with a single
 * 16KB memcpy (p8_blit_layer fast path).  The full-screen background fill
 * is deferred when issued and consumed by the composite.  The sparse mask-8
 * foreground keeps its own sentinel buffer, blitted only over its occupied
 * row band (most rooms: none).
 *
 * Two knobs:
 *   celeste_glue_no_bg (default ON) -- skip the mask-4 decor layer entirely:
 *     only walls/terrain are visible, per playtest feedback ("background
 *     muddies the visuals").  Also saves its compose cost.
 *   celeste_glue_use_cache -- A/B toggle; with the cache OFF the reference
 *     per-tile path runs.  With no_bg=0, cache on/off must be byte-identical
 *     (tools/host_test.c verifies).  KNOWN approximation with the cache ON:
 *     moving platforms and the big chest draw between the two map layers in
 *     the original order, so solid terrain should occlude them; with the
 *     composed buffer they draw on top.  Platforms travel through open air,
 *     so this is invisible in practice.
 *
 * The cache is bypassed whenever pal() is not the identity (title flash).
 * music()/sfx() are stubs until the audio phase.
 */
#include <stdarg.h>
#include <string.h>

#include "celeste.h"
#include "celeste_glue_typed.h"
#include "p8.h"
#include "mac_input.h"
#include "gfx_data.h"

void mac_sound_play(int sfx_id);   /* mac_sound.c on Mac; stubbed on host */
void mac_music_play(int index);
void mac_display_set_room(const unsigned char *room);  /* donut principle */

int celeste_glue_last_music = -1;   /* observable by tests / future audio */
int celeste_glue_use_cache  = 1;    /* runtime toggle (host A/B testing) */
int celeste_glue_rebuilds   = 0;    /* diag: room-cache rebuild counter */
int celeste_glue_no_bg      = 1;    /* skip mask-4 bg decor layer */

/* ---- room cache ---- */
static unsigned char g_static[P8_W * P8_H];  /* fill + (bg) + solid terrain */
static unsigned char g_fg[P8_W * P8_H];      /* mask-8 overlay, 0xFF = clear */
static int g_fg_y0, g_fg_y1;                 /* occupied fg row band */
static int g_key_mx = -1, g_key_my = -1, g_key_off, g_key_bgcol, g_key_nobg;
static int g_cache_valid = 0;
static int g_pending_bg = -1;       /* deferred full-screen fill color */

/* ccleste's exact masking rule, incl. the flags==4 hack */
static int tile_in_layer(int tile, int mask)
{
    return mask == 0 ||
           (mask == 4 && p8_fget(tile) == 4) ||
           p8_fget_bit(tile, mask != 4 ? mask - 1 : mask);
}

/* draw one 8x8 tile into an index buffer; color 0 is transparent (PICO-8
 * map() default) and leaves the buffer byte untouched */
static void tile_to_buf(unsigned char *buf, int tile, int dx, int dy)
{
    const unsigned char *src = gfx_data + (tile / 16) * 8 * GFX_W + (tile % 16) * 8;
    int y, x;
    for (y = 0; y < 8; y++) {
        int py = dy + y;
        if (py < 0 || py >= P8_H) continue;
        for (x = 0; x < 8; x++) {
            int px = dx + x;
            unsigned char c = src[y * GFX_W + x];
            if (c != 0 && px >= 0 && px < P8_W)
                buf[py * P8_W + px] = c;
        }
    }
}

static void build_room_cache(int mx, int my, int off, int bgcol)
{
    int x, y;
    memset(g_static, bgcol & 15, sizeof g_static);
    memset(g_fg, 0xFF, sizeof g_fg);
    g_fg_y0 = P8_H; g_fg_y1 = -1;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            int tile = p8_mget(mx + x, my + y);
            if (tile == 0) continue;
            if (!celeste_glue_no_bg && tile_in_layer(tile, 4))
                tile_to_buf(g_static, tile, x * 8, y * 8);
            if (tile_in_layer(tile, 2))
                tile_to_buf(g_static, tile, x * 8 + off, y * 8);
            if (tile_in_layer(tile, 8)) {
                tile_to_buf(g_fg, tile, x * 8, y * 8);
                if (y * 8 < g_fg_y0)     g_fg_y0 = y * 8;
                if (y * 8 + 7 > g_fg_y1) g_fg_y1 = y * 8 + 7;
            }
        }
    }
    g_key_mx = mx; g_key_my = my; g_key_off = off;
    g_key_bgcol = bgcol & 15; g_key_nobg = celeste_glue_no_bg;
    g_cache_valid = 1;
    celeste_glue_rebuilds++;          /* diag: rebuilds/frame == the bug */
    mac_display_set_room(g_static);   /* pre-dither the new room once */
}

/* uncached reference path (also the pal()-active fallback) */
static void map_direct(int mx, int my, int tx, int ty, int mw, int mh, int mask)
{
    int x, y;
    if (celeste_glue_no_bg && mask == 4) return;
    for (x = 0; x < mw; x++) {
        for (y = 0; y < mh; y++) {
            int tile = p8_mget(mx + x, my + y);
            if (tile != 0 && tile_in_layer(tile, mask))
                p8_spr(tile, tx + x * 8, ty + y * 8, 1, 1, 0, 0);
        }
    }
}

/* run the deferred full-screen fill before any other drawing happens */
static void flush_pending_fill(void)
{
    if (g_pending_bg >= 0) {
        p8_rectfill(0, 0, P8_W, P8_H, g_pending_bg);
        g_pending_bg = -1;
    }
}

/* ---- typed direct-call entry points (celeste_glue_typed.h) ----
 * Same logic as the varargs switch below, one function per PICO-8 op, called
 * directly by ccleste's P8xxx wrappers with no va_arg/switch dispatch. */

void glue_music(int track, int fade, int mask)
{
    (void)fade; (void)mask;
    celeste_glue_last_music = track;
    mac_music_play(track);              /* -1 stops; host_test stubs this */
}

void glue_spr(int sprite, int x, int y, int cols, int rows, int flipx, int flipy)
{
    (void)cols; (void)rows;             /* celeste only draws 1x1 (asserted upstream) */
    flush_pending_fill();
    if (sprite >= 0)
        p8_spr(sprite, x, y, 1, 1, flipx, flipy);
}

int glue_btn(int b) { return p8_btn(b) ? 1 : 0; }

void glue_sfx(int id) { mac_sound_play(id); }   /* host_test provides a stub */

void glue_pal(int a, int b)
{
    if (a >= 0 && a < 16 && b >= 0 && b < 16)
        p8_pal(a, b);
}

void glue_pal_reset(void) { p8_pal_reset(); }

void glue_circfill(int cx, int cy, int r, int col)
{
    flush_pending_fill();
    p8_circfill(cx, cy, r, col);
}

void glue_print(const char *str, int x, int y, int col)
{
    col %= 16;
    /* 3x5 glyphs turn to mush under any dither pattern: at 1-bit, text
     * is either black (kept, for light boxes) or white */
    if (col != 0) col = 7;
    /* title button prompt: swap the original "x+c" for this port's full
     * control scheme, re-centered for the longer string (same trick
     * ccleste's 3DS build uses for "a+b") */
    if (str[0] == 'x' && str[1] == '+' && str[2] == 'c' && str[3] == 0) {
        str = "wasd move  enter jump  ' dash";
        x = (P8_W - p8_print_width(str)) / 2;
    }
    flush_pending_fill();
    p8_print(str, x, y, col);
}

void glue_rectfill(int x0, int y0, int x1, int y1, int col)
{
    if (celeste_glue_use_cache &&
        x0 <= 0 && y0 <= 0 && x1 >= P8_W && y1 >= P8_H) {
        flush_pending_fill();           /* two full fills in a row: run the 1st */
        g_pending_bg = col & 15;        /* defer; the mask-4 map consumes it */
    } else {
        flush_pending_fill();
        p8_rectfill(x0, y0, x1, y1, col);
    }
}

void glue_line(int x0, int y0, int x1, int y1, int col)
{
    flush_pending_fill();
    p8_line(x0, y0, x1, y1, col);
}

int glue_mget(int tx, int ty) { return p8_mget(tx, ty); }

void glue_camera(int x, int y) { p8_camera(x, y); }

int glue_fget(int tile, int flag) { return p8_fget_bit(tile, flag); }

void glue_map(int mx, int my, int tx, int ty, int mw, int mh, int mask)
{
    int cacheable = celeste_glue_use_cache && p8_pal_is_default() &&
                    mw == 16 && mh == 16 && ty == 0 &&
                    (mask == 4 || mask == 2 || mask == 8) &&
                    (mask == 2 || tx == 0);
    if (cacheable && mask == 4 && g_pending_bg >= 0) {
        /* the composite replaces fill + bg decor + solid terrain */
        if (!g_cache_valid || g_key_mx != mx || g_key_my != my ||
            g_key_bgcol != g_pending_bg || g_key_nobg != celeste_glue_no_bg) {
            build_room_cache(mx, my, g_key_off, g_pending_bg);
            g_pending_bg = -1;
            p8_blit_layer(g_static, -1, 0, P8_H - 1);   /* new room: all */
            return;
        }
        g_pending_bg = -1;
        /* frame-start restore: erase last frame's CONTENT rows (sprites
         * + overlays) back to room.  Everything else in p8_screen still
         * IS the room.  Reset RENDER (display accumulates it fresh this
         * frame) and CONTENT (this frame's draws repopulate it).  The
         * restore marks render (rows changed) but NOT content (it's
         * room, nothing to erase) -- that's what makes the set shrink. */
        {
            unsigned char erase[P8_H];
            short ex0[P8_H], ex1[P8_H];
            int y;
            memcpy(erase, p8_content_row, P8_H);
            memcpy(ex0, p8_content_x0, sizeof ex0);   /* last frame's spans */
            memcpy(ex1, p8_content_x1, sizeof ex1);
            p8_dirty_reset();
            p8_content_reset();
            /* dirty-rectangle erase: restore only the column span each row's
             * sprites occupied last frame, not the full 128-px row. */
            for (y = 0; y < P8_H; y++)
                if (erase[y]) p8_restore_span(g_static, y, ex0[y], ex1[y]);
        }
        return;
    }
    if (cacheable && mask == 2 &&
        g_cache_valid && g_key_mx == mx && g_key_my == my) {
        if (g_key_off != tx) {          /* title's x-offset: bake and re-blit */
            build_room_cache(mx, my, tx, g_key_bgcol);
            p8_blit_layer(g_static, -1, 0, P8_H - 1);
        }
        return;                         /* terrain already in the composite */
    }
    if (cacheable && mask == 8 &&
        g_cache_valid && g_key_mx == mx && g_key_my == my) {
        /* overlay only the rows that were restored or drawn to THIS
         * frame -- rows untouched since last frame already carry the
         * fg from then.  Blitting the whole band would re-mark it
         * dirty every frame and defeat the dirty-row present. */
        if (g_fg_y1 >= g_fg_y0) {
            int y = g_fg_y0;
            while (y <= g_fg_y1) {
                if (p8_dirty_row[y]) {
                    int y1 = y;
                    while (y1 + 1 <= g_fg_y1 && p8_dirty_row[y1 + 1]) y1++;
                    p8_blit_layer(g_fg, 0xFF, y, y1);
                    y = y1 + 1;
                } else y++;
            }
        }
        return;
    }
    flush_pending_fill();
    map_direct(mx, my, tx, ty, mw, mh, mask);
}

/* Legacy varargs dispatcher -- kept so Celeste_P8_set_call_func() still has a
 * valid target and any non-Mac caller keeps working.  The Mac build's P8xxx
 * wrappers bypass this entirely (CELESTE_TYPED_CALLS), calling glue_* direct. */
int celeste_glue_p8call(CELESTE_P8_CALLBACK_TYPE call, ...)
{
    va_list args;
    int ret = 0;
    va_start(args, call);

#define INT_ARG()  va_arg(args, int)

    switch (call) {
    case CELESTE_P8_MUSIC: { int t = INT_ARG(), f = INT_ARG(), m = INT_ARG();
        glue_music(t, f, m); } break;
    case CELESTE_P8_SPR: { int sp = INT_ARG(), x = INT_ARG(), y = INT_ARG();
        int c = INT_ARG(), r = INT_ARG(), fx = INT_ARG(), fy = INT_ARG();
        glue_spr(sp, x, y, c, r, fx, fy); } break;
    case CELESTE_P8_BTN: { int b = INT_ARG(); ret = glue_btn(b); } break;
    case CELESTE_P8_SFX: { int id = INT_ARG(); glue_sfx(id); } break;
    case CELESTE_P8_PAL: { int a = INT_ARG(), b = INT_ARG(); glue_pal(a, b); } break;
    case CELESTE_P8_PAL_RESET: glue_pal_reset(); break;
    case CELESTE_P8_CIRCFILL: { int x = INT_ARG(), y = INT_ARG(), r = INT_ARG(), c = INT_ARG();
        glue_circfill(x, y, r, c); } break;
    case CELESTE_P8_PRINT: { const char *s = va_arg(args, const char *);
        int x = INT_ARG(), y = INT_ARG(), c = INT_ARG(); glue_print(s, x, y, c); } break;
    case CELESTE_P8_RECTFILL: { int x0 = INT_ARG(), y0 = INT_ARG(), x1 = INT_ARG(), y1 = INT_ARG(), c = INT_ARG();
        glue_rectfill(x0, y0, x1, y1, c); } break;
    case CELESTE_P8_LINE: { int x0 = INT_ARG(), y0 = INT_ARG(), x1 = INT_ARG(), y1 = INT_ARG(), c = INT_ARG();
        glue_line(x0, y0, x1, y1, c); } break;
    case CELESTE_P8_MGET: { int x = INT_ARG(), y = INT_ARG(); ret = glue_mget(x, y); } break;
    case CELESTE_P8_CAMERA: { int x = INT_ARG(), y = INT_ARG(); glue_camera(x, y); } break;
    case CELESTE_P8_FGET: { int t = INT_ARG(), f = INT_ARG(); ret = glue_fget(t, f); } break;
    case CELESTE_P8_MAP: { int mx = INT_ARG(), my = INT_ARG(), tx = INT_ARG(), ty = INT_ARG();
        int mw = INT_ARG(), mh = INT_ARG(), mask = INT_ARG();
        glue_map(mx, my, tx, ty, mw, mh, mask); } break;
    }

    va_end(args);
    return ret;
}
