/*
 * mac_prefs.c -- high-score persistence via the classic File Manager.
 *
 * Scores live in a small data file ("JustOneBoss Scores", 4 longs) created
 * next to the app on the default volume/directory -- the period-typical spot
 * for a game's score file on System 6/7.  All errors are swallowed: a locked
 * disk or missing file just means scores don't persist this session.
 */
#include "game.h"

#include <Files.h>
#include <MacTypes.h>

static const unsigned char kScoreFile[] = "\pJustOneBoss Scores";

void platform_load_scores(long s[4])
{
    short ref;
    long count = 4 * (long)sizeof(long);
    if (FSOpen(kScoreFile, 0, &ref) != noErr)
        return;                         /* no file yet -- first run */
    FSRead(ref, &count, (void *)s);
    FSClose(ref);
}

void platform_save_scores(const long s[4])
{
    short ref;
    long count = 4 * (long)sizeof(long);
    /* dupFNErr on later saves is fine; any other failure just skips saving */
    Create(kScoreFile, 0, 'JOBs', 'PREF');
    if (FSOpen(kScoreFile, 0, &ref) != noErr)
        return;
    SetFPos(ref, fsFromStart, 0);
    FSWrite(ref, &count, (const void *)s);
    FSClose(ref);
}
