/*
 * p8.h -- a faithful reimplementation of the subset of the PICO-8 drawing API
 *         that Just One Boss uses, rendering into a 128x128 byte framebuffer of
 *         palette indices (0..15).
 *
 * Game code calls these exactly like it called PICO-8's builtins, so the ported
 * Lua reads almost line-for-line.  The framebuffer is converted to 1-bit B&W
 * once per frame by mac_display.c -- p8.c itself knows nothing about the Mac.
 *
 * Only the API surface actually used by the cart is implemented (verified by
 * grepping the .p8):  cls, camera, clip, pset/pget, rect, rectfill, line,
 * circfill, color, pal, palt, spr, sspr, map, print, fget, and the math the
 * game leans on.  See PORTING-PLAN.md section 2.
 */
#ifndef P8_HEADER
#define P8_HEADER

#define P8_W 128
#define P8_H 128

/* the logical screen: one byte per pixel, value = current palette index 0..15.
 * mac_display.c reads this each frame. */
extern unsigned char p8_screen[P8_W * P8_H];

/* ---- lifecycle ---- */
void p8_init(void);           /* load gfx/flags/map, reset draw state */
void p8_reset_draw_state(void); /* pal(), palt() defaults, camera 0, clip full */

/* ---- draw-state ---- */
void p8_cls(void);            /* clear to color 0 */
void p8_camera(int x, int y); /* subtract (x,y) from all subsequent draws */
void p8_camera_reset(void);
void p8_clip(int x, int y, int w, int h);
void p8_clip_reset(void);
void p8_color(int c);         /* set default draw color */

/* pal(c0,c1) remaps draw color c0 -> c1. pal() with no args resets.
 * palt(c,t) sets/clears color c as transparent. palt() resets (only 0 clear). */
void p8_pal(int c0, int c1);
void p8_pal_reset(void);
void p8_palt(int c, int transparent);
void p8_palt_reset(void);

/* ---- primitives (coordinates are pre-camera; p8.c applies the camera) ---- */
void p8_pset(int x, int y, int c);
int  p8_pget(int x, int y);
void p8_rectfill(int x0, int y0, int x1, int y1, int c);
void p8_rect(int x0, int y0, int x1, int y1, int c);
void p8_line(int x0, int y0, int x1, int y1, int c);
void p8_circfill(int x, int y, int r, int c);

/* ---- sprites ----
 * spr : draw sprite n (8x8 cell) at (x,y). w,h in cells. flip flags.
 * sspr: draw a sw x sh rect from the sheet at (sx,sy) to (dx,dy) sized dw x dh.
 *       (Just One Boss always draws 1:1 scale, so dw==sw and dh==sh, but we
 *        support scaling for completeness.)               */
void p8_spr(int n, int x, int y, int w, int h, int flip_x, int flip_y);
void p8_sspr(int sx, int sy, int sw, int sh,
             int dx, int dy, int dw, int dh, int flip_x, int flip_y);

/* ---- map / flags ---- */
void p8_map(int cell_x, int cell_y, int sx, int sy, int cw, int ch);
int  p8_mget(int cx, int cy);
/* fget(n)     -> full flag byte for sprite n
 * fget_bit(n,b) -> bit b of that byte (0/1) */
int  p8_fget(int n);
int  p8_fget_bit(int n, int b);

/* ---- layer-cache support (used by celeste_glue's room cache) ----
 * p8_pal_is_default: 1 if pal() is the identity map (cached raw indices are
 *                    then safe to blit without remapping).
 * p8_blit_layer: copy source rows [sy0..sy1] of a 128x128 index buffer onto
 *                the screen, applying the current camera offset and clip.
 *                sentinel >= 0 marks transparent bytes; pass -1 for opaque.
 *                Opaque + no camera + full clip + full band = one memcpy. */
int  p8_pal_is_default(void);
void p8_blit_layer(const unsigned char *buf, int sentinel, int sy0, int sy1);

/* ---- dirty rows (two sets; see p8.c) ----
 * p8_dirty_row[]   RENDER set: rows mac_display must re-dither+blit.
 * p8_content_row[] ERASE set: rows with sprite/overlay content the glue
 *                  must restore to room next frame. */
extern unsigned char p8_dirty_row[P8_H];
extern unsigned char p8_content_row[P8_H];
void p8_dirty_reset(void);
void p8_content_reset(void);

/* per-row changed-column spans (dirty-rectangle optimization; source px,
 * inclusive, x0>x1 = empty).  _dirty_ = this frame's render span (present
 * reads it); _content_ = this frame's content span (glue saves for erase). */
extern short p8_dirty_x0[P8_H], p8_dirty_x1[P8_H];
extern short p8_content_x0[P8_H], p8_content_x1[P8_H];
/* restore one row's [x0..x1] span from a layer buffer (room) -- span-aware erase */
void p8_restore_span(const unsigned char *buf, int y, int x0, int x1);

/* ---- text ---- */
void p8_print(const char *s, int x, int y, int c);
int  p8_print_width(const char *s); /* 4px per char, PICO-8 convention */

#endif /* P8_HEADER */
