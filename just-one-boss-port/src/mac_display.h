/*
 * mac_display.h -- converts the p8 128x128 index framebuffer to a 256x256
 * 1-bit image (2x scale + ordered dither) and CopyBits it to the game window.
 */
#ifndef MAC_DISPLAY_H
#define MAC_DISPLAY_H

#include <MacTypes.h>
#include <Windows.h>

#define DISP_SCALE   2
#define DISP_W       (128 * DISP_SCALE)   /* 256 */
#define DISP_H       (128 * DISP_SCALE)   /* 256 */

/* allocate the offscreen 1-bit buffer; remember the window to blit into */
OSErr mac_display_init(WindowPtr win);
void  mac_display_dispose(void);

/* dither p8_screen -> offscreen -> CopyBits onto the window
 * (only rows changed since the previous frame are re-dithered/blitted) */
void  mac_display_present(void);

/* donut principle: pre-dither the static background once; present() then
 * only dithers bytes where sprites differ from it.  NULL disables. */
void  mac_display_set_room(const unsigned char *room);
/* force a full redraw on the next present (call on window update events) */
void  mac_display_invalidate(void);

/* where the 256x256 image sits inside the window's content area */
extern Rect gDisplayDstRect;

#endif /* MAC_DISPLAY_H */
