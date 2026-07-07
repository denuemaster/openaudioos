#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define OAOS_I2S_BCLK_GPIO  11
#define OAOS_I2S_DOUT_GPIO  12
#define OAOS_I2S_LRCK_GPIO  13
#define OAOS_SAMPLE_RATE    44100

typedef struct {
    bool enabled;
    int frequency_hz;
    int volume;
    uint64_t frames_written;
    uint32_t underruns;
} oaos_audio_state_t;

esp_err_t oaos_audio_init(void);
void oaos_audio_start(void);
void oaos_audio_stop(void);
void oaos_audio_set_volume(int volume);
void oaos_audio_set_frequency(int frequency_hz);
oaos_audio_state_t oaos_audio_get_state(void);
