#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#define OAOS_SETUP_AP_SSID      "OpenAudioOS-Setup"
#define OAOS_SETUP_AP_PASSWORD  "openaudio"
#define OAOS_HOSTNAME           "openaudioos"

esp_err_t oaos_wifi_init(void);
bool oaos_wifi_is_configured(void);
bool oaos_wifi_has_saved_config(void);
const char *oaos_wifi_get_ssid(void);
esp_ip4_addr_t oaos_wifi_get_ip(void);
