/* count_test.c -- headless op-count profiler for the physics.
 * Counts _fix32 multiplies/divides per frame during real gameplay, split
 * between game_update (physics) and game_draw (rendering).  Build with
 * -DFIX_COUNT so celeste.c's operators bump the counters. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p8.h"
#include "game.h"

unsigned long g_fix_mul = 0, g_fix_div = 0;   /* bumped by celeste.c operators */
#ifdef PX_COUNT
extern unsigned long g_px_sprite, g_px_restore, g_px_fill;   /* from p8.c */
extern unsigned char p8_dirty_row[];
extern short p8_dirty_x0[], p8_dirty_x1[];
extern short p8_content_x0[], p8_content_x1[];
#endif

static int g_buttons;
int p8_btn(int b)  { return (g_buttons >> b) & 1; }
int p8_btnp(int b) { return (g_buttons >> b) & 1; }   /* held == pressed, enough to drive */
void mac_sound_play(int sfx_id) { (void)sfx_id; }
void mac_music_play(int index)  { (void)index; }
void mac_display_set_room(const unsigned char *room) { (void)room; }

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 300;
    const char *script = argc > 2 ? argv[2] : "";
    int warmup = argc > 3 ? atoi(argv[3]) : 0;   /* frames to exclude from stats */
    int i, counted = 0;
    unsigned long um = 0, ud = 0, dm = 0, dd = 0;   /* update/draw mul/div totals */
    unsigned long dirty_total = 0, dirty_span_total = 0, content_span_total = 0;
#ifdef PX_COUNT
    extern int celeste_glue_use_cache;
    (void)celeste_glue_use_cache;
#endif

    game_init(1234u);
    for (i = 0; i < frames; i++) {
        const char *s = script;
        unsigned long m0, d0;
        while (*s) {
            int fr = atoi(s);
            const char *colon = strchr(s, ':');
            if (colon && fr == i) g_buttons = atoi(colon + 1);
            s = strchr(s, ','); if (!s) break; s++;
        }
        if (i == warmup) {               /* start of the measured window: zero all */
            um = ud = dm = dd = dirty_total = dirty_span_total = content_span_total = 0;
#ifdef PX_COUNT
            g_px_sprite = g_px_restore = g_px_fill = 0;
#endif
        }
        m0 = g_fix_mul; d0 = g_fix_div;
        game_update();
        um += g_fix_mul - m0; ud += g_fix_div - d0;
        m0 = g_fix_mul; d0 = g_fix_div;
        game_draw();
        dm += g_fix_mul - m0; dd += g_fix_div - d0;
#ifdef PX_COUNT
        if (i >= warmup) {
            int r;
            for (r = 0; r < P8_H; r++) {
                if (p8_dirty_row[r]) dirty_total++;
                if (p8_dirty_x1[r] >= p8_dirty_x0[r])
                    dirty_span_total += p8_dirty_x1[r] - p8_dirty_x0[r] + 1;
                if (p8_content_x1[r] >= p8_content_x0[r])
                    content_span_total += p8_content_x1[r] - p8_content_x0[r] + 1;
            }
        }
#endif
        if (i >= warmup) counted++;
    }
    if (counted < 1) counted = 1;
    printf("frames measured=%d (warmup skipped=%d)\n", counted, warmup);
    printf("UPDATE per frame:  mul=%lu  div=%lu\n", um / counted, ud / counted);
    printf("DRAW   per frame:  mul=%lu  div=%lu\n", dm / counted, dd / counted);
    printf("--- 68000 cost estimate (mul~70cyc, div~120cyc incl. helper overhead) ---\n");
    {
        unsigned long upd_cyc = (um / counted) * 70 + (ud / counted) * 120;
        printf("UPDATE fixedp cycles/frame ~= %lu  (~%.2f ms @ 7.83MHz)\n",
               upd_cyc, upd_cyc / 7830.0);
    }
#define frames counted   /* the PX_COUNT report below divides by the measured window */
#ifdef PX_COUNT
    printf("--- DRAW pixel writes to p8_screen (index bytes) per frame ---\n");
    printf("  room restore = %lu   sprites = %lu   fills = %lu   TOTAL = %lu bytes/frame\n",
           g_px_restore / frames, g_px_sprite / frames, g_px_fill / frames,
           (g_px_restore + g_px_sprite + g_px_fill) / frames);
    printf("--- PRESENT workload ---\n");
    printf("  dirty rows/frame = %lu  (current: full 128px/row)\n", dirty_total / frames);
    printf("--- DIRTY-RECTANGLE savings (per-row column spans) ---\n");
    printf("  restore: full-row %lu B  ->  span %lu B  (%.0f%% of current)\n",
           dirty_total / frames * 128, content_span_total / frames,
           100.0 * (content_span_total / (double)frames) / ((dirty_total / (double)frames) * 128 + 1));
    printf("  present: full-row %lu px -> span %lu px  (%.0f%% of current)\n",
           dirty_total / frames * 128, dirty_span_total / frames,
           100.0 * (dirty_span_total / (double)frames) / ((dirty_total / (double)frames) * 128 + 1));
    {
        unsigned long draw_b = (g_px_restore + g_px_sprite + g_px_fill) / frames;
        unsigned long pres_b = (dirty_total / frames) * (32 + 64);  /* dither 32B + blit 2x32B per row */
        printf("--- rough 68000 memory-op estimate (~4 cyc/byte touched) ---\n");
        printf("  DRAW  ~= %lu cyc (~%.1f ms)   PRESENT ~= %lu cyc (~%.1f ms)\n",
               draw_b*4, draw_b*4/7830.0, pres_b*4, pres_b*4/7830.0);
    }
#endif
    return 0;
}
