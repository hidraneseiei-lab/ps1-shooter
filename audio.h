#ifndef AUDIO_H
#define AUDIO_H
#include "audio_ids.h"

/* Lagu */
enum { SONG_NONE = -1, SONG_MENU = 0, SONG_PLAY = 1, SONG_BOSS = 2 };

void audioInit(void);
void audioUpdate(void);          /* panggil SEKALI per frame */
void sfxPlay(int sampleId);      /* SND_* dari audio_data.h */
void musicPlay(int song);        /* SONG_* ; aman dipanggil berulang (abaikan bila sudah main) */
void musicStop(void);
int  musicCurrent(void);

#endif
