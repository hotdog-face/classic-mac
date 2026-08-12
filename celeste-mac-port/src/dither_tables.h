/*
 * dither_tables.h -- the 16 PICO-8 colors as hand-designed 8x8 1-bit patterns,
 * shared by mac_display.c and tools/host_test.c so host renders are pixel-
 * identical to the Mac.
 *
 * Readability-first (not luminance-first): at 1-bit, adjacent mid-grays turn
 * to mush, so each *material* gets a distinct TEXTURE -- classic Mac game
 * technique.  Bit set = black pixel; bit 7 = leftmost of 8 output columns.
 * The x-period (8) equals one output byte, so a solid color row is one table
 * byte -- this is what mac_display.c's fast path banks on.
 *
 * Color roles verified against an index dump of the real first room:
 * sky = 0 (solid black) with big drifting CLOUDS = 12; terrain interiors are
 * 5 (stone) and 1 (deep rock) with white 7 snow edges; Madeline = 8 hair +
 * 15 face + 11/3 green body.  Scheme: DARK sky, LIGHT terrain masses, so
 * platforms and the white snow/spike edges carry the silhouette.
 */
#ifndef DITHER_TABLES_H
#define DITHER_TABLES_H

static const unsigned char kPattern[16][8] = {
    /*  0 black:   sky, shadow cavities -> solid black */
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
    /*  1 navy:    deep rock interior, bg walls -> dense 62% mesh */
    {0xEE,0x55,0xBB,0x55,0xEE,0x55,0xBB,0x55},
    /*  2 purple:  old-site bg hills -> heavy diagonal hatch 75% */
    {0xEE,0xDD,0xBB,0x77,0xEE,0xDD,0xBB,0x77},
    /*  3 dk green: Madeline's jacket (also grass tufts) -> solid white:
     *  the player reads as a clean line-art figure, no dither ("muddy") */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /*  4 brown:   dirt -> light diagonal hatch 19% */
    {0x88,0x44,0x22,0x11,0x88,0x44,0x22,0x11},
    /*  5 dk gray: stone interior (most terrain) -> fine 25% checker (light mass) */
    {0xAA,0x00,0x55,0x00,0xAA,0x00,0x55,0x00},
    /*  6 lt gray: jump/land SMOKE PUFFS + spike shading -> solid white so
     *  the character's effects match her line-art look (the bg snowbanks
     *  that once wanted this dark are hidden entirely in the walls-only
     *  build, freeing the color) */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /*  7 white:   snow edges, spikes, text -> solid white */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /*  8 red:     hair, berries, springs -> 50% checker, matching the
     *  spent-dash hair (12): the owner prefers the checkered "dashing"
     *  hair at ALL times over a white/gray dash-state indicator */
    {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55},
    /*  9 orange:  crumbles, flashes -> diagonal dots 25% */
    {0x44,0x11,0x44,0x11,0x44,0x11,0x44,0x11},
    /* 10 yellow:  sparkles -> near-white 3% */
    {0x00,0x20,0x00,0x00,0x00,0x02,0x00,0x00},
    /* 11 green:   jacket highlights, berry leaf -> solid white (crisp player) */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 12 lt blue: SPENT-dash hair + ice (bg snowbanks that used this are now
     *             hidden entirely) -> 50% checker: "depleted" gray head vs the
     *             solid-white full-dash hair, and ice rooms stay visible */
    {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55},
    /* 13 lavender: title logo/mountain art (bg hills are hidden) -> solid
     *  white so the wordmark and mountain read as clean line art */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 14 pink:    balloons, flag -> loose 25% dots */
    {0x22,0x88,0x22,0x88,0x22,0x88,0x22,0x88},
    /* 15 peach:   Madeline's face -> solid white (crisp player) */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

#endif /* DITHER_TABLES_H */
