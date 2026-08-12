/*
 * mac_prefs.c -- best-run records ("Celeste Prefs" file, default volume).
 *
 * Uses the original File Manager calls (Create/FSOpen/FSRead/FSWrite) so it
 * works on System 6.  The file holds one little struct; best time, most
 * berries and fewest deaths are each tracked independently, and a save
 * happens only on a completed run (summit flag reached).
 */
#include "mac_prefs.h"

#include <MacTypes.h>
#include <Files.h>
#include <string.h>
#include <stdio.h>

#define PREFS_MAGIC   0x434C5331L   /* 'CLS1' */
#define PREFS_NAME    "\pCeleste Prefs"

typedef struct {
    long  magic;
    long  best_secs;      /* fastest completion, in seconds; -1 = none */
    short best_fruit;     /* most berries on a completed run */
    short best_deaths;    /* fewest deaths on a completed run; -1 = none */
    long  completions;    /* total summit runs */
} Prefs;

static Prefs gPrefs;

static void prefs_reset(void)
{
    gPrefs.magic = PREFS_MAGIC;
    gPrefs.best_secs = -1;
    gPrefs.best_fruit = 0;
    gPrefs.best_deaths = -1;
    gPrefs.completions = 0;
}

void mac_prefs_load(void)
{
    short ref;
    long count = sizeof gPrefs;

    prefs_reset();
    if (FSOpen(PREFS_NAME, 0, &ref) == noErr) {
        Prefs p;
        if (FSRead(ref, &count, &p) == noErr &&
            count == sizeof p && p.magic == PREFS_MAGIC)
            gPrefs = p;
        FSClose(ref);
    }
}

static void prefs_save(void)
{
    short ref;
    long count = sizeof gPrefs;

    Create(PREFS_NAME, 0, 'CLST', 'PREF');   /* dupFNErr is fine */
    if (FSOpen(PREFS_NAME, 0, &ref) == noErr) {
        FSWrite(ref, &count, &gPrefs);
        FSClose(ref);
        FlushVol(0, 0);
    }
}

void celeste_mac_completed(int fruit, int death_count, int min, int sec)
{
    long total = (long)min * 60 + sec;
    int better = 0;

    gPrefs.completions++;
    if (gPrefs.best_secs < 0 || total < gPrefs.best_secs) {
        gPrefs.best_secs = total;
        better = 1;
    }
    if (fruit > gPrefs.best_fruit) {
        gPrefs.best_fruit = (short)fruit;
        better = 1;
    }
    if (gPrefs.best_deaths < 0 || death_count < gPrefs.best_deaths) {
        gPrefs.best_deaths = (short)death_count;
        better = 1;
    }
    (void)better;
    prefs_save();                    /* completions changed regardless */
}

int mac_prefs_best_line(char *buf)
{
    if (gPrefs.best_secs < 0) return 0;
    sprintf(buf, "best %02ld:%02ld  %d berries",
            gPrefs.best_secs / 60, gPrefs.best_secs % 60,
            (int)gPrefs.best_fruit);
    return 1;
}
