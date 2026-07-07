#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    OAOS_AP_ADAPTER_STACK_NONE = 0,
    OAOS_AP_ADAPTER_STACK_INTERNAL_PLACEHOLDER = 1,
    OAOS_AP_ADAPTER_STACK_SHAIRPORT_SYNC_PORT = 2,
    OAOS_AP_ADAPTER_STACK_RBOUTEILLER_AIRPLAY_ESP32 = 3,
    OAOS_AP_ADAPTER_STACK_CUSTOM_RAOP = 4,
} oaos_airplay_adapter_stack_t;

typedef struct {
    bool initialized;
    oaos_airplay_adapter_stack_t selected_stack;
    const char *selected_stack_name;
    uint64_t pcm_frames_received;
    uint64_t pcm_frames_forwarded;
    uint32_t pcm_push_errors;
    uint32_t source_claims;
    uint32_t source_releases;
} oaos_airplay_adapter_status_t;

esp_err_t oaos_airplay_adapter_init(void);
esp_err_t oaos_airplay_adapter_claim_source(void);
esp_err_t oaos_airplay_adapter_release_source(void);
esp_err_t oaos_airplay_adapter_push_pcm_s16_stereo(const int16_t *frames, size_t frame_count, uint32_t timeout_ms);
oaos_airplay_adapter_status_t oaos_airplay_adapter_get_status(void);
const char *oaos_airplay_adapter_stack_name(oaos_airplay_adapter_stack_t stack);
