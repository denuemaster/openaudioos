#include "oaos_airplay.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "oaos_audio.h"

static const char *TAG = "oaos_airplay";

#define AIRPLAY_PLACEHOLDER_CHUNK_FRAMES 256

static SemaphoreHandle_t ap_mutex;
static bool enabled = true;
static bool placeholder_streaming = false;
static oaos_airplay_state_t state = OAOS_AIRPLAY_STATE_IDLE;
static uint64_t start_time_us = 0;
static uint32_t sessions_started = 0;
static uint32_t packets_received = 0;
static uint32_t frames_pushed = 0;
static uint32_t errors = 0;

const char *oaos_airplay_state_name(oaos_airplay_state_t s)
{
    switch (s) {
        case OAOS_AIRPLAY_STATE_DISABLED: return "disabled";
        case OAOS_AIRPLAY_STATE_IDLE: return "idle";
        case OAOS_AIRPLAY_STATE_ADVERTISING: return "advertising";
        case OAOS_AIRPLAY_STATE_CONNECTED: return "connected";
        case OAOS_AIRPLAY_STATE_STREAMING: return "streaming";
        case OAOS_AIRPLAY_STATE_ERROR: return "error";
        default: return "unknown";
    }
}

static void set_state_locked(oaos_airplay_state_t new_state)
{
    if (state != new_state) {
        ESP_LOGI(TAG, "AirPlay state: %s -> %s", oaos_airplay_state_name(state), oaos_airplay_state_name(new_state));
        state = new_state;
    }
}

/*
 * Placeholder task:
 * This is NOT AirPlay protocol implementation.
 * It validates that an AirPlay source can push PCM into the audio engine using
 * OAOS_AUDIO_SOURCE_AIRPLAY. The real RAOP/AirPlay implementation will replace
 * this producer.
 */
static void airplay_placeholder_task(void *arg)
{
    int16_t buffer[AIRPLAY_PLACEHOLDER_CHUNK_FRAMES * OAOS_CHANNELS];
    double phase = 0.0;

    ESP_LOGI(TAG, "AirPlay placeholder producer started");

    while (true) {
        bool run = false;

        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        run = enabled && placeholder_streaming;
        if (run) {
            set_state_locked(OAOS_AIRPLAY_STATE_STREAMING);
        } else if (enabled) {
            set_state_locked(OAOS_AIRPLAY_STATE_IDLE);
        } else {
            set_state_locked(OAOS_AIRPLAY_STATE_DISABLED);
        }
        xSemaphoreGive(ap_mutex);

        if (!run) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        const double step = 2.0 * M_PI * 880.0 / (double)OAOS_SAMPLE_RATE;
        const int amplitude = 6000;

        for (int i = 0; i < AIRPLAY_PLACEHOLDER_CHUNK_FRAMES; i++) {
            int16_t sample = (int16_t)(sin(phase) * amplitude);
            phase += step;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
            buffer[i * 2] = sample;
            buffer[i * 2 + 1] = sample;
        }

        esp_err_t err = oaos_audio_push_pcm_from_source(
            OAOS_AUDIO_SOURCE_AIRPLAY,
            buffer,
            AIRPLAY_PLACEHOLDER_CHUNK_FRAMES,
            20
        );

        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        packets_received++;
        if (err == ESP_OK) {
            frames_pushed += AIRPLAY_PLACEHOLDER_CHUNK_FRAMES;
        } else {
            errors++;
        }
        xSemaphoreGive(ap_mutex);

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t oaos_airplay_init(void)
{
    ap_mutex = xSemaphoreCreateMutex();
    if (!ap_mutex) return ESP_ERR_NO_MEM;

    start_time_us = esp_timer_get_time();

    ESP_LOGI(TAG, "AirPlay foundation initialized");
    ESP_LOGI(TAG, "Real RAOP/AirPlay protocol is not implemented yet");
    ESP_LOGI(TAG, "M0.8 exposes source plumbing and status APIs only");

    xTaskCreatePinnedToCore(airplay_placeholder_task, "oaos_airplay_ph", 4096, NULL, 7, NULL, 0);
    return ESP_OK;
}

esp_err_t oaos_airplay_enable(void)
{
    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    enabled = true;
    set_state_locked(OAOS_AIRPLAY_STATE_IDLE);
    xSemaphoreGive(ap_mutex);
    return ESP_OK;
}

esp_err_t oaos_airplay_disable(void)
{
    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    enabled = false;
    placeholder_streaming = false;
    set_state_locked(OAOS_AIRPLAY_STATE_DISABLED);
    xSemaphoreGive(ap_mutex);
    return ESP_OK;
}

esp_err_t oaos_airplay_start_placeholder_stream(void)
{
    oaos_audio_set_active_source(OAOS_AUDIO_SOURCE_AIRPLAY);

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    if (!placeholder_streaming) {
        sessions_started++;
    }
    enabled = true;
    placeholder_streaming = true;
    set_state_locked(OAOS_AIRPLAY_STATE_STREAMING);
    xSemaphoreGive(ap_mutex);

    ESP_LOGW(TAG, "Started AirPlay placeholder stream. This is a local 880 Hz test source, not real AirPlay.");
    return ESP_OK;
}

esp_err_t oaos_airplay_stop_placeholder_stream(void)
{
    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    placeholder_streaming = false;
    set_state_locked(enabled ? OAOS_AIRPLAY_STATE_IDLE : OAOS_AIRPLAY_STATE_DISABLED);
    xSemaphoreGive(ap_mutex);
    return ESP_OK;
}

oaos_airplay_status_t oaos_airplay_get_status(void)
{
    oaos_airplay_status_t s;

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    s.enabled = enabled;
    s.state = state;
    s.state_name = oaos_airplay_state_name(state);
    s.uptime_ms = (esp_timer_get_time() - start_time_us) / 1000;
    s.sessions_started = sessions_started;
    s.packets_received = packets_received;
    s.frames_pushed = frames_pushed;
    s.errors = errors;
    s.device_name = "OpenAudioOS";
    s.protocol_note = "M0.8 foundation only: real AirPlay/RAOP not implemented yet";
    xSemaphoreGive(ap_mutex);

    return s;
}
