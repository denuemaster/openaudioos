#include "esp_log.h"
#include "esp_err.h"

#include "oaos_system.h"
#include "oaos_storage.h"
#include "oaos_wifi.h"
#include "oaos_web.h"
#include "oaos_audio.h"

static const char *TAG = "OpenAudioOS";

void app_main(void)
{
    ESP_LOGI(TAG, "OpenAudioOS M0.4 starting");

    ESP_ERROR_CHECK(oaos_system_init());
    ESP_ERROR_CHECK(oaos_storage_init());
    ESP_ERROR_CHECK(oaos_wifi_init());
    ESP_ERROR_CHECK(oaos_web_init());
    ESP_ERROR_CHECK(oaos_audio_init());

    ESP_LOGI(TAG, "OpenAudioOS M0.4 ready");
}
