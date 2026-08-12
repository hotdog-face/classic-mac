/*
 * celeste_glue_typed.h -- typed direct-call entry points into the PICO-8 glue.
 *
 * ccleste issues hundreds of P8xxx calls per frame.  Upstream routes every one
 * through a single varargs function pointer + a 14-way switch (Celeste_P8_call).
 * On a 68000 that dispatch (va_start/va_arg marshalling, indirect call, switch)
 * is pure overhead repeated on the hottest path in the game -- and profiling
 * showed game_draw, not present, is the real framerate wall.
 *
 * These typed functions are the same case bodies, called DIRECTLY: no varargs,
 * no indirect call, no switch.  celeste.c's P8xxx wrappers call these when
 * CELESTE_TYPED_CALLS is defined (the Mac build); the legacy varargs
 * celeste_glue_p8call() is kept as a thin dispatcher for anything else.
 */
#ifndef CELESTE_GLUE_TYPED_H_
#define CELESTE_GLUE_TYPED_H_

#ifdef __cplusplus
extern "C" {
#endif

void glue_music(int track, int fade, int mask);
void glue_spr(int sprite, int x, int y, int cols, int rows, int flipx, int flipy);
int  glue_btn(int b);
void glue_sfx(int id);
void glue_pal(int a, int b);
void glue_pal_reset(void);
void glue_circfill(int x, int y, int r, int col);
void glue_print(const char *str, int x, int y, int col);
void glue_rectfill(int x0, int y0, int x1, int y1, int col);
void glue_line(int x0, int y0, int x1, int y1, int col);
int  glue_mget(int tx, int ty);
void glue_camera(int x, int y);
int  glue_fget(int tile, int flag);
void glue_map(int mx, int my, int tx, int ty, int mw, int mh, int mask);

#ifdef __cplusplus
}
#endif

#endif
