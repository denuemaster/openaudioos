#include "oaos_audio.h"

#include <math.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "oaos_audio";

#define OAOS_AUDIO_RINGBUFFER_BYTES (64 * 1024)
#define OAOS_SOURCE_CHUNK_FRAMES    256
#define OAOS_OUTPUT_CHUNK_FRAMES    256

static i2s_chan_handle_t tx_chan = NULL;
static SemaphoreHandle_t audio_mutex = NULL;
static RingbufHandle_t pcm_ringbuffer = NULL;

static oaos_audio_state_t audio_state = {
    .active_source = OAOS_AUDIO_SOURCE_TEST_TONE,
    .enabled = true,
    .frequency_hz = 440,
    .volume = 25,
    .source_frames_generated = 0,
    .source_frames_pushed = 0,
    .output_frames_written = 0,
    .buffer_underruns = 0,
    .buffer_overruns = 0,
    .i2s_errors = 0,
    .source_switches = 0,
    .ringbuffer_free_bytes = 0,
    .ringbuffer_used_bytes = 0,
};

const char *oaos_audio_source_name(oaos_audio_source_type_t source)
{
    switch (source) {
        case OAOS_AUDIO_SOURCE_NONE: return "none";
        case OAOS_AUDIO_SOURCE_TEST_TONE: return "test_tone";
        case OAOS_AUDIO_SOURCE_AIRPLAY: return "airplay";
        case OAOS_AUDIO_SOURCE_USB_AUDIO: return "usb_audio";
        case OAOS_AUDIO_SOURCE_SPOTIFY: return "spotify";
        case OAOS_AUDIO_SOURCE_BLUETOOTH: return "bluetooth";
        case OAOS_AUDIO_SOURCE_DLNA: return "dlna";
        default: return "unknown";
    }
}

static void update_ringbuffer_stats_locked(void)
{
    if (!pcm_ringbuffer) {
        audio_state.ringbuffer_free_bytes = 0;
        audio_state.ringbuffer_used_bytes = 0;
        return;
    }

    size_t free_bytes = xRingbufferGetCurFreeSize(pcm_ringbuffer);
    audio_state.ringbuffer_free_bytes = free_bytes;
    audio_state.ringbuffer_used_bytes = OAOS_AUDIO_RINGBUFFER_BYTES > free_bytes
                                      ? OAOS_AUDIO_RINGBUFFER_BYTES - free_bytes
                                      : 0;
}

static void drain_ringbuffer(void)
{
    if (!pcm_ringbuffer) return;

    while (true) {
        size_t item_size = 0;
        void *item = xRingbufferReceive(pcm_ringbuffer, &item_size, 0);
        if (!item) break;
        vRingbufferReturnItem(pcm_ringbuffer, item);
    }
}

esp_err_t oaos_audio_set_active_source(oaos_audio_source_type_t source)
{
    if (source < OAOS_AUDIO_SOURCE_NONE || source > OAOS_AUDIO_SOURCE_DLNA) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    if (audio_state.active_source != source) {
        ESP_LOGI(TAG, "Audio source switch: %s -> %s",
                 oaos_audio_source_name(audio_state.active_source),
                 oaos_audio_source_name(source));
        audio_state.active_source = source;
        audio_state.source_switches++;
        drain_ringbuffer();
        update_ringbuffer_stats_locked();
    }
    xSemaphoreGive(audio_mutex);

    return ESP_OK;
}

oaos_audio_source_type_t oaos_audio_get_active_source(void)
{
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    oaos_audio_source_type_t source = audio_state.active_source;
    xSemaphoreGive(audio_mutex);
    return source;
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 512;

    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_chan, NULL), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(OAOS_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = OAOS_I2S_BCLK_GPIO,
            .ws = OAOS_I2S_LRCK_GPIO,
            .dout = OAOS_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {.mclk_inv=false, .bclk_inv=false, .ws_inv=false},
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_chan, &std_cfg), TAG, "i2s std init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_chan), TAG, "i2s enable failed");

    ESP_LOGI(TAG, "I2S initialized: BCLK=%d DOUT=%d LRCK=%d sample_rate=%d",
             OAOS_I2S_BCLK_GPIO, OAOS_I2S_DOUT_GPIO, OAOS_I2S_LRCK_GPIO, OAOS_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t oaos_audio_push_pcm_from_source(
    oaos_audio_source_type_t source,
    const int16_t *stereo_frames,
    size_t frame_count,
    uint32_t timeout_ms
) {
    if (!pcm_ringbuffer || !stereo_frames || frame_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    oaos_audio_source_type_t active = audio_state.active_source;
    xSemaphoreGive(audio_mutex);

    if (source != active) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes = frame_count * OAOS_CHANNELS * sizeof(int16_t);
    BaseType_t ok = xRingbufferSend(pcm_ringbuffer, stereo_frames, bytes, pdMS_TO_TICKS(timeout_ms));

    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    if (ok != pdTRUE) {
        audio_state.buffer_overruns++;
        update_ringbuffer_stats_locked();
        xSemaphoreGive(audio_mutex);
        return ESP_ERR_TIMEOUT;
    }

    audio_state.source_frames_pushed += frame_count;
    update_ringbuffer_stats_locked();
    xSemaphoreGive(audio_mutex);
    return ESP_OK;
}

esp_err_t oaos_audio_push_pcm(const int16_t *stereo_frames, size_t frame_count, uint32_t timeout_ms)
{
    return oaos_audio_push_pcm_from_source(
        oaos_audio_get_active_source(),
        stereo_frames,
        frame_count,
        timeout_ms
    );
}

static void test_tone_source_task(void *arg)
{
    int16_t buffer[OAOS_SOURCE_CHUNK_FRAMES * OAOS_CHANNELS];
    double phase = 0.0;

    ESP_LOGI(TAG, "Test tone source started");

    while (true) {
        oaos_audio_state_t snapshot = oaos_audio_get_state();

        if (snapshot.active_source != OAOS_AUDIO_SOURCE_TEST_TONE) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        double step = 2.0 * M_PI * (double)snapshot.frequency_hz / (double)OAOS_SAMPLE_RATE;
        int amplitude = (snapshot.volume * 30000) / 100;

        for (int i = 0; i < OAOS_SOURCE_CHUNK_FRAMES; i++) {
            int16_t sample = 0;
            if (snapshot.enabled && snapshot.volume > 0) {
                sample = (int16_t)(sin(phase) * amplitude);
            }

            phase += step;
            if (phase >= 2.0 * M_PI) {
                phase -= 2.0 * M_PI;
            }

            buffer[i * 2] = sample;
            buffer[i * 2 + 1] = sample;
        }

        esp_err_t err = oaos_audio_push_pcm_from_source(
            OAOS_AUDIO_SOURCE_TEST_TONE,
            buffer,
            OAOS_SOURCE_CHUNK_FRAMES,
            20
        );

        xSemaphoreTake(audio_mutex, portMAX_DELAY);
        if (err == ESP_OK) {
            audio_state.source_frames_generated += OAOS_SOURCE_CHUNK_FRAMES;
        }
        xSemaphoreGive(audio_mutex);

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void i2s_output_task(void *arg)
{
    int16_t silence[OAOS_OUTPUT_CHUNK_FRAMES * OAOS_CHANNELS] = {0};

    ESP_LOGI(TAG, "I2S output task started");

    while (true) {
        size_t item_size = 0;
        int16_t *item = (int16_t *)xRingbufferReceiveUpTo(
            pcm_ringbuffer,
            &item_size,
            pdMS_TO_TICKS(20),
            OAOS_OUTPUT_CHUNK_FRAMES * OAOS_CHANNELS * sizeof(int16_t)
        );

        const int16_t *out = silence;
        size_t out_bytes = sizeof(silence);
        bool underrun = false;

        if (item && item_size > 0) {
            out = item;
            out_bytes = item_size;
        } else {
            underrun = true;
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(tx_chan, out, out_bytes, &bytes_written, portMAX_DELAY);

        if (item) {
            vRingbufferReturnItem(pcm_ringbuffer, item);
        }

        xSemaphoreTake(audio_mutex, portMAX_DELAY);
        if (underrun) {
            audio_state.buffer_underruns++;
        }
        if (err == ESP_OK && bytes_written == out_bytes) {
            audio_state.output_frames_written += bytes_written / (OAOS_CHANNELS * sizeof(int16_t));
        } else {
            audio_state.i2s_errors++;
        }
        update_ringbuffer_stats_locked();
        xSemaphoreGive(audio_mutex);

        if (err != ESP_OK || bytes_written != out_bytes) {
            ESP_LOGW(TAG, "I2S write issue: err=%s bytes=%u expected=%u",
                     esp_err_to_name(err), (unsigned)bytes_written, (unsigned)out_bytes);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

esp_err_t oaos_audio_init(void)
{
    audio_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(audio_mutex != NULL, ESP_ERR_NO_MEM, TAG, "mutex failed");

    pcm_ringbuffer = xRingbufferCreate(OAOS_AUDIO_RINGBUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
    ESP_RETURN_ON_FALSE(pcm_ringbuffer != NULL, ESP_ERR_NO_MEM, TAG, "ringbuffer failed");

    ESP_RETURN_ON_ERROR(init_i2s(), TAG, "init_i2s failed");

    xTaskCreatePinnedToCore(i2s_output_task, "oaos_i2s_out", 4096, NULL, 20, NULL, 1);
    xTaskCreatePinnedToCore(test_tone_source_task, "oaos_tone_src", 4096, NULL, 8, NULL, 0);

    ESP_LOGI(TAG, "Audio engine initialized with %d byte ringbuffer", OAOS_AUDIO_RINGBUFFER_BYTES);
    return ESP_OK;
}

void oaos_audio_start(void)
{
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    audio_state.enabled = true;
    xSemaphoreGive(audio_mutex);
}

void oaos_audio_stop(void)
{
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    audio_state.enabled = false;
    xSemaphoreGive(audio_mutex);
}

void oaos_audio_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    audio_state.volume = volume;
    xSemaphoreGive(audio_mutex);
}

void oaos_audio_set_frequency(int frequency_hz)
{
    if (frequency_hz < 50) frequency_hz = 50;
    if (frequency_hz > 20000) frequency_hz = 20000;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    audio_state.frequency_hz = frequency_hz;
    xSemaphoreGive(audio_mutex);
}

oaos_audio_state_t oaos_audio_get_state(void)
{
    oaos_audio_state_t snapshot;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    update_ringbuffer_stats_locked();
    snapshot = audio_state;
    xSemaphoreGive(audio_mutex);
    return snapshot;
}
