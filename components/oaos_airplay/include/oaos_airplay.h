#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    OAOS_AIRPLAY_STATE_DISABLED = 0,
    OAOS_AIRPLAY_STATE_IDLE = 1,
    OAOS_AIRPLAY_STATE_ADVERTISING = 2,
    OAOS_AIRPLAY_STATE_CONNECTED = 3,
    OAOS_AIRPLAY_STATE_FP_SETUP = 4,
    OAOS_AIRPLAY_STATE_STREAMING = 5,
    OAOS_AIRPLAY_STATE_ERROR = 6,
} oaos_airplay_state_t;

typedef struct {
    bool enabled;
    bool mdns_started;
    bool raop_advertised;
    bool airplay_advertised;
    bool rtsp_listener_started;
    int rtsp_port;
    oaos_airplay_state_t state;
    const char *state_name;
    uint64_t uptime_ms;
    uint32_t sessions_started;
    uint32_t rtsp_connections;
    uint32_t rtsp_requests;
    uint32_t rtsp_options;
    uint32_t rtsp_info;
    uint32_t rtsp_fp_setup;
    uint32_t rtsp_pair_setup;
    uint32_t rtsp_pair_verify;
    uint32_t rtsp_announce;
    uint32_t rtsp_setup;
    uint32_t rtsp_record;
    uint32_t rtsp_teardown;
    uint32_t packets_received;
    uint32_t frames_pushed;
    uint32_t errors;
    uint32_t last_content_length;
    uint8_t last_fp_header[16];
    const char *device_name;
    const char *raop_instance;
    const char *protocol_note;
} oaos_airplay_status_t;

esp_err_t oaos_airplay_init(void);
esp_err_t oaos_airplay_enable(void);
esp_err_t oaos_airplay_disable(void);
esp_err_t oaos_airplay_start_placeholder_stream(void);
esp_err_t oaos_airplay_stop_placeholder_stream(void);
oaos_airplay_status_t oaos_airplay_get_status(void);
const char *oaos_airplay_state_name(oaos_airplay_state_t state);
