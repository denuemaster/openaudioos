#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    OAOS_AP_BACKEND_NONE = 0,
    OAOS_AP_BACKEND_INTERNAL_DISCOVERY = 1,
    OAOS_AP_BACKEND_AIRPLAY_ESP32_CANDIDATE = 2,
    OAOS_AP_BACKEND_SHAIRPORT_CANDIDATE = 3,
} oaos_airplay_backend_type_t;

typedef struct {
    bool initialized;
    bool third_party_present;
    bool third_party_compiled;
    oaos_airplay_backend_type_t backend_type;
    const char *backend_name;
    const char *third_party_path;
    const char *integration_state;
    uint32_t init_count;
    uint32_t warnings;
} oaos_airplay_backend_status_t;

esp_err_t oaos_airplay_backend_init(void);
oaos_airplay_backend_status_t oaos_airplay_backend_get_status(void);
const char *oaos_airplay_backend_name(oaos_airplay_backend_type_t type);
