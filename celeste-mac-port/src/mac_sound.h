/*
 * mac_sound.h -- Celeste SFX through the classic Sound Manager: one sampled
 * channel, a new sound interrupts the current one (fine for retro SFX).
 */
#ifndef MAC_SOUND_H
#define MAC_SOUND_H

int  mac_sound_init(void);     /* 0 on success; failure = silent, not fatal */
void mac_sound_play(int sfx_id);
void mac_music_play(int index); /* celeste music() index (0,10,..); -1 stops */
void mac_music_stop(void);
void mac_music_idle(void);      /* once per frame: keeps the loop queued */
void mac_sound_dispose(void);

#endif /* MAC_SOUND_H */
