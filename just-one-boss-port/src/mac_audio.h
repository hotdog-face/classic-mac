/*
 * mac_audio.h -- Sound Manager playback of the pre-rendered SFX samples
 * (see tools/synth_sfx.py and src/generated/sfx_data.*).
 */
#ifndef MAC_AUDIO_H
#define MAC_AUDIO_H

int  mac_audio_init(void);      /* 1 on success, 0 if sound unavailable */
void mac_audio_dispose(void);
void mac_audio_tick(void);      /* call every main-loop pass: resumes/loops music */

#endif /* MAC_AUDIO_H */
