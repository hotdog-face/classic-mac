/*
 * host_test.c -- compile the PORTABLE half of the port (p8 emulation + engine +
 * game) on the dev machine, run N frames, and dump the framebuffer through the
 * exact same 1-bit dither the Mac uses, to a PGM image.  Lets us verify the
 * whole data->emulation->dither pipeline without Retro68 or an emulator.
 *
 * Build: cc -Isrc -Isrc/generated tools/host_test.c src/p8.c src/engine.c \
 *           src/util.c src/entities.c src/game.c src/generated/*.c -lm -o host_test
 */
#include <stdio.h>
#include <stdlib.h>
#include "p8.h"
#include "game.h"
void platform_set_room(const unsigned char *room) { (void)room; }
#include "engine.h"
#include "entities.h"

/* host stubs for the Mac-only input layer (mac_input.c is not compiled here).
 * argv-driven scripted input lets us exercise movement headlessly. */
static int g_fake_btnp[6];
int p8_btn(int b)  { (void)b; return 0; }
int p8_btnp(int b) { return (b >= 0 && b < 6) ? g_fake_btnp[b] : 0; }
void platform_sfx(int n) { (void)n; }     /* no audio on the host build */
void platform_music(int id) { (void)id; }

/* host score persistence: a real file, so the save->load->hard-mode-unlock
 * flow can be verified headlessly (mirrors mac_prefs.c) */
#define HOST_SCORES "/tmp/job_scores.bin"
void platform_load_scores(long s[4])
{
    FILE *f = fopen(HOST_SCORES, "rb");
    if (!f) return;
    if (fread(s, sizeof(long), 4, f) != 4) { /* short file: keep defaults */ }
    fclose(f);
}
void platform_save_scores(const long s[4])
{
    FILE *f = fopen(HOST_SCORES, "wb");
    if (!f) return;
    fwrite(s, sizeof(long), 4, f);
    fclose(f);
}

/* mirror of mac_display.c's dither tables (keep in sync!) */
static const unsigned char kBlackness[16] = {
    64,58,50,46,44,50,4,0, 54,14,6,16,18,20,22,8
};
static const unsigned char kBayer8[8][8] = {
    {  0,32, 8,40, 2,34,10,42},{ 48,16,56,24,50,18,58,26},
    { 12,44, 4,36,14,46, 6,38},{ 60,28,52,20,62,30,54,22},
    {  3,35,11,43, 1,33, 9,41},{ 51,19,59,27,49,17,57,25},
    { 15,47, 7,39,13,45, 5,37},{ 63,31,55,23,61,29,53,21}
};
#define OUT_W 256
#define OUT_H 256

static void dump_pgm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int sy, sx;
    fprintf(f, "P5\n%d %d\n255\n", OUT_W, OUT_H);
    for (sy = 0; sy < OUT_H; sy++) {
        const unsigned char *brow = kBayer8[sy & 7];
        int py = sy >> 1;
        for (sx = 0; sx < OUT_W; sx++) {
            int px = sx >> 1;
            unsigned char c = p8_screen[py * P8_W + px];
            unsigned char v = (kBlackness[c & 15] > brow[sx & 7]) ? 0 : 255;
            fputc(v, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 60;
    int i;
    const char *out = argc > 2 ? argv[2] : "frame.pgm";

    int play = (argc > 3 && argv[3][0] == 'p');
    char mode = (argc > 3) ? argv[3][0] : 0;
    int boss = (mode == 'b' || mode == '2' || mode == '3' || mode == '4' || mode == 'h');
    int winlose = (mode == 'v' || mode == 'g');
    int phase2 = (mode == '2');
    int tutor  = (mode == 't');
    if (winlose) boss = 1;   /* drive through start so a player/stage exists */
    game_init();
    for (i = 0; i < frames; i++) {
        int b;
        for (b = 0; b < 6; b++) g_fake_btnp[b] = 0;
        if (play || boss || tutor) {
            if (i == 10) g_fake_btnp[1] = 1;                 /* press start */
            if (i == 150 || i == 162 || i == 174) g_fake_btnp[1] = 1; /* right */
            if (i == 156 || i == 168) g_fake_btnp[3] = 1;    /* down */
        }
        if (boss && i == 70) {
            if (mode == '3')      entities_debug_set_phase(3);
            else if (mode == '4') entities_debug_set_phase(4);
            else if (mode == 'h') { entities_debug_hard(); entities_debug_set_phase(2); }
            else if (phase2)      entities_debug_set_phase(2);
            else                  entities_debug_spawn_boss_fight();
        }
        if (mode == 'v' && i == 70) entities_debug_win();
        if (mode == 'g' && i == 70) entities_debug_lose();
        /* mode 't': drive the REAL tutorial->boss path by teleporting the
         * player onto each magic tile (exercises collection/milestones/intro) */
        if (tutor && g_player) {
            int k;
            for (k = 0; k < g_entity_count; k++) {
                Entity *t = g_entities[k];
                if (t && t->class_id == 15) { g_player->x = t->x; g_player->y = t->y; break; }
            }
        }
        game_update();
        game_draw();
    }
    dump_pgm(out);
    printf("ran %d frames, wrote %s (%dx%d, 1-bit dithered)\n",
           frames, out, OUT_W, OUT_H);
    if (tutor)
        printf("tutorial result: boss_phase=%d bar=%d boss=%s\n",
               g_boss_phase,
               g_boss_health ? (int)g_boss_health->n[0] : -1,
               g_boss ? "spawned" : "none");
    return 0;
}
