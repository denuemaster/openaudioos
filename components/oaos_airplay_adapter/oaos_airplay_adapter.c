#include "oaos_airplay_adapter.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "oaos_audio.h"

static const char *TAG = "oaos_airplay_adapter";

static SemaphoreHandle_t adapter_mutex;
static bool initialized = false;
static oaos_airplay_adapter_stack_t selected_stack = OAOS_AP_ADAPTER_STACK_INTERNAL_PLACEHOLDER;
static uint64_t pcm_frames_received = 0;
static uint64_t pcm_frames_forwarded = 0;
static uint32_t pcm_push_errors = 0;
static uint32_t source_claims = 0;
static uint32_t source_releases = 0;

const char *oaos_airplay_adapter_stack_name(oaos_airplay_adapter_stack_t stack)
{
    switch (stack) {
        case OAOS_AP_ADAPTER_STACK_NONE: return "none";
        case OAOS_AP_ADAPTER_STACK_INTERNAL_PLACEHOLDER: return "internal_placeholder";
        case OAOS_AP_ADAPTER_STACK_SHAIRPORT_SYNC_PORT: return "shairport_sync_port";
        case OAOS_AP_ADAPTER_STACK_RBOUTEILLER_AIRPLAY_ESP32: return "rbouteiller_airplay_esp32";
        case OAOS_AP_ADAPTER_STACK_CUSTOM_RAOP: return "custom_raop";
        default: return "unknown";
    }
}

esp_err_t oaos_airplay_adapter_init(void)
{
    adapter_mutex = xSemaphoreCreateMutex();
    if (!adapter_mutex) {
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(adapter_mutex, portMAX_DELAY);
    initialized = true;
    xSemaphoreGive(adapter_mutex);

    ESP_LOGI(TAG, "AirPlay adapter initialized");
    ESP_LOGI(TAG, "Selected stack strategy: %s", oaos_airplay_adapter_stack_name(selected_stack));
    ESP_LOGW(TAG, "M0.13 is an adapter layer only. No third-party AirPlay stack is compiled in yet.");

    return ESP_OK;
}

esp_err_t oaos_airplay_adapter_claim_source(void)
{
    esp_err_t err = oaos_audio_set_active_source(OAOS_AUDIO_SOURCE_AIRPLAY);

    xSemaphoreTake(adapter_mutex, portMAX_DELAY);
    if (err == ESP_OK) {
        source_claims++;
    } else {
        pcm_push_errors++;
    }
    xSemaphoreGive(adapter_mutex);

    return err;
}

esp_err_t oaos_airplay_adapter_release_source(void)
{
    xSemaphoreTake(adapter_mutex, portMAX_DELAY);
    source_releases++;
    xSemaphoreGive(adapter_mutex);

    /*
     * Do not automatically switch back to test tone here.
     * The source manager should later decide fallback policy.
     */
    return ESP_OK;
}

esp_err_t oaos_airplay_adapter_push_pcm_s16_stereo(const int16_t *frames, size_t frame_count, uint32_t timeout_ms)
{
    if (!frames || frame_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(adapter_mutex, portMAX_DELAY);
    pcm_frames_received += frame_count;
    xSemaphoreGive(adapter_mutex);

    esp_err_t err = oaos_audio_push_pcm_from_source(
        OAOS_AUDIO_SOURCE_AIRPLAY,
        frames,
        frame_count,
        timeout_ms
    );

    xSemaphoreTake(adapter_mutex, portMAX_DELAY);
    if (err == ESP_OK) {
        pcm_frames_forwarded += frame_count;
    } else {
        pcm_push_errors++;
    }
    xSemaphoreGive(adapter_mutex);

    return err;
}

oaos_airplay_adapter_status_t oaos_airplay_adapter_get_status(void)
{
    oaos_airplay_adapter_status_t s;

    xSemaphoreTake(adapter_mutex, portMAX_DELAY);
    s.initialized = initialized;
    s.selected_stack = selected_stack;
    s.selected_stack_name = oaos_airplay_adapter_stack_name(selected_stack);
    s.pcm_frames_received = pcm_frames_received;
    s.pcm_frames_forwarded = pcm_frames_forwarded;
    s.pcm_push_errors = pcm_push_errors;
    s.source_claims = source_claims;
    s.source_releases = source_releases;
    xSemaphoreGive(adapter_mutex);

    return s;
}
