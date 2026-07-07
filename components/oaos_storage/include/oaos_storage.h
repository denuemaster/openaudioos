#pragma once
#include <stddef.h>
#include "esp_err.h"

esp_err_t oaos_storage_init(void);
esp_err_t oaos_storage_load_wifi(char *ssid, size_t ssid_len, char *password, size_t password_len);
esp_err_t oaos_storage_save_wifi(const char *ssid, const char *password);
esp_err_t oaos_storage_clear_wifi(void);
