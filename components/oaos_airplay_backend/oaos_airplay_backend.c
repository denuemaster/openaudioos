#include "oaos_airplay_backend.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "oaos_airplay_backend";

static SemaphoreHandle_t backend_mutex;
static bool initialized = false;
static bool third_party_present = false;
static bool third_party_compiled = false;
static oaos_airplay_backend_type_t backend_type = OAOS_AP_BACKEND_INTERNAL_DISCOVERY;
static uint32_t init_count = 0;
static uint32_t warnings = 0;

const char *oaos_airplay_backend_name(oaos_airplay_backend_type_t type)
{
    switch (type) {
        case OAOS_AP_BACKEND_NONE: return "none";
        case OAOS_AP_BACKEND_INTERNAL_DISCOVERY: return "internal_discovery";
        case OAOS_AP_BACKEND_AIRPLAY_ESP32_CANDIDATE: return "rbouteiller_airplay_esp32_candidate";
        case OAOS_AP_BACKEND_SHAIRPORT_CANDIDATE: return "shairport_candidate";
        default: return "unknown";
    }
}

esp_err_t oaos_airplay_backend_init(void)
{
    backend_mutex = xSemaphoreCreateMutex();
    if (!backend_mutex) {
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(backend_mutex, portMAX_DELAY);
    initialized = true;
    init_count++;
    warnings++;
    xSemaphoreGive(backend_mutex);

    ESP_LOGI(TAG, "AirPlay backend selector initialized");
    ESP_LOGI(TAG, "Current backend: %s", oaos_airplay_backend_name(backend_type));
    ESP_LOGW(TAG, "M0.14 does not compile third-party AirPlay yet.");
    ESP_LOGW(TAG, "Fetch candidate with: tools/fetch_airplay_esp32.sh");
    ESP_LOGW(TAG, "Next milestone ports selected backend into OpenAudioOS adapter.");

    return ESP_OK;
}

oaos_airplay_backend_status_t oaos_airplay_backend_get_status(void)
{
    oaos_airplay_backend_status_t s;

    xSemaphoreTake(backend_mutex, portMAX_DELAY);
    s.initialized = initialized;
    s.third_party_present = third_party_present;
    s.third_party_compiled = third_party_compiled;
    s.backend_type = backend_type;
    s.backend_name = oaos_airplay_backend_name(backend_type);
    s.third_party_path = "third_party/airplay-esp32";
    s.integration_state = "evaluation_scaffold_only";
    s.init_count = init_count;
    s.warnings = warnings;
    xSemaphoreGive(backend_mutex);

    return s;
}
