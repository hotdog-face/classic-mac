/*
 * mac_prefs.h -- persistent best-run records in a "Celeste Prefs" file next
 * to the app (classic File Manager, System 6-safe).
 */
#ifndef MAC_PREFS_H
#define MAC_PREFS_H

void mac_prefs_load(void);            /* call once at startup */

/* fired by the celeste.c completion hook (CELESTE_MAC_HOOKS) */
void celeste_mac_completed(int fruit, int death_count, int min, int sec);

/* formats "best 05:43  12 berries" into buf (>= 32 bytes);
 * returns 0 if there is no completed run on record yet */
int mac_prefs_best_line(char *buf);

#endif /* MAC_PREFS_H */
