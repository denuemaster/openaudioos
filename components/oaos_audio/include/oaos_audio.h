#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define OAOS_I2S_BCLK_GPIO  11
#define OAOS_I2S_DOUT_GPIO  12
#define OAOS_I2S_LRCK_GPIO  13
#define OAOS_SAMPLE_RATE    44100
#define OAOS_CHANNELS       2

typedef enum {
    OAOS_AUDIO_SOURCE_NONE = 0,
    OAOS_AUDIO_SOURCE_TEST_TONE = 1,
    OAOS_AUDIO_SOURCE_AIRPLAY = 2,
    OAOS_AUDIO_SOURCE_USB_AUDIO = 3,
    OAOS_AUDIO_SOURCE_SPOTIFY = 4,
    OAOS_AUDIO_SOURCE_BLUETOOTH = 5,
    OAOS_AUDIO_SOURCE_DLNA = 6,
} oaos_audio_source_type_t;

typedef struct {
    oaos_audio_source_type_t active_source;
    bool enabled;
    int frequency_hz;
    int volume;
    uint64_t source_frames_generated;
    uint64_t source_frames_pushed;
    uint64_t output_frames_written;
    uint32_t buffer_underruns;
    uint32_t buffer_overruns;
    uint32_t i2s_errors;
    uint32_t source_switches;
    size_t ringbuffer_free_bytes;
    size_t ringbuffer_used_bytes;
} oaos_audio_state_t;

esp_err_t oaos_audio_init(void);
void oaos_audio_start(void);
void oaos_audio_stop(void);
void oaos_audio_set_volume(int volume);
void oaos_audio_set_frequency(int frequency_hz);
oaos_audio_state_t oaos_audio_get_state(void);

const char *oaos_audio_source_name(oaos_audio_source_type_t source);
esp_err_t oaos_audio_set_active_source(oaos_audio_source_type_t source);
oaos_audio_source_type_t oaos_audio_get_active_source(void);

/*
 * Source API:
 * AirPlay, USB Audio, Spotify, Bluetooth and WebRadio should push signed
 * 16-bit stereo PCM frames into this function.
 */
esp_err_t oaos_audio_push_pcm_from_source(
    oaos_audio_source_type_t source,
    const int16_t *stereo_frames,
    size_t frame_count,
    uint32_t timeout_ms
);

/* Backwards-compatible helper. Uses current active source. */
esp_err_t oaos_audio_push_pcm(const int16_t *stereo_frames, size_t frame_count, uint32_t timeout_ms);
