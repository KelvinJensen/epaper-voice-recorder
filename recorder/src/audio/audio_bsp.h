#ifndef AUDIO_BSP_H
#define AUDIO_BSP_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_bsp_init(void);
void audio_play_init(void);
void audio_prepare_sleep(void);
void audio_idle_suspend(void);
void audio_playback_set_vol(uint8_t vol);
int audio_playback_read(void *data_ptr, uint32_t len);
int audio_playback_write(void *data_ptr, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
