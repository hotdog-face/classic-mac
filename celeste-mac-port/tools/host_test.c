/*
 * host_test.c -- run the portable half of the Celeste port (p8 emulation +
 * celeste.c game logic) on the dev machine for N frames with scripted input,
 * and dump the framebuffer through the exact 1-bit dither the Mac uses.
 *
 * Build: cc -std=c99 -Isrc -Isrc/generated tools/host_test.c src/p8.c \
 *           src/celeste.c src/celeste_glue.c src/game.c src/generated/*.c \
 *           -lm -o host_test
 *
 * Usage: host_test [frames] [out.pgm] [script] [use_cache]
 *   script: comma-separated  frame:buttonmask  pairs, mask bits 0..5 =
 *   left,right,up,down,jump(z),dash(x); button held from that frame until
 *   the next entry.  e.g. "10:16,20:0" holds jump for frames 10..19.
 *   use_cache: 1 (default) / 0 -- toggles the glue's room cache; runs with
 *   both values must produce byte-identical images.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p8.h"
#include "game.h"

/* host stand-ins for mac_input.c / mac_sound.c */
static int g_buttons;
int p8_btn(int b)  { return (g_buttons >> b) & 1; }
int p8_btnp(int b) { return 0; }
void mac_sound_play(int sfx_id) { (void)sfx_id; }
void mac_music_play(int index)  { (void)index; }
void mac_display_set_room(const unsigned char *room) { (void)room; }

/* the exact per-color patterns the Mac blit uses */
#include "dither_tables.h"
#define OUT_W 256
#define OUT_H 256

static void dump_pgm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int sy, sx;
    fprintf(f, "P5\n%d %d\n255\n", OUT_W, OUT_H);
    for (sy = 0; sy < OUT_H; sy++) {
        int py = sy >> 1;
        for (sx = 0; sx < OUT_W; sx++) {
            int px = sx >> 1;
            unsigned char c = p8_screen[py * P8_W + px] & 15;
            int black = (kPattern[c][sy & 7] >> (7 - (sx & 7))) & 1;
            fputc(black ? 0 : 255, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    extern int celeste_glue_use_cache, celeste_glue_no_bg;
    int frames = argc > 1 ? atoi(argv[1]) : 60;
    const char *out = argc > 2 ? argv[2] : "frame.pgm";
    const char *script = argc > 3 ? argv[3] : "";
    int i;

    if (argc > 4) celeste_glue_use_cache = atoi(argv[4]);
    if (argc > 5) celeste_glue_no_bg = atoi(argv[5]);   /* default 1 = Mac look */
    game_init(1234u);   /* fixed seed for reproducible runs */
    for (i = 0; i < frames; i++) {
        /* apply any script entries due this frame */
        const char *s = script;
        while (*s) {
            int fr = atoi(s);
            const char *colon = strchr(s, ':');
            if (colon && fr == i) g_buttons = atoi(colon + 1);
            s = strchr(s, ',');
            if (!s) break;
            s++;
        }
        game_update();
        game_draw();
    }
    dump_pgm(out);
    printf("ran %d frames, wrote %s (%dx%d, 1-bit dithered)\n",
           frames, out, OUT_W, OUT_H);
    return 0;
}
