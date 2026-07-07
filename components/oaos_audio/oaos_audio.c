#include "oaos_audio.h"

#include <math.h>
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "oaos_audio";

static i2s_chan_handle_t tx_chan = NULL;
static SemaphoreHandle_t audio_mutex;

static oaos_audio_state_t audio_state = {
    .enabled = true,
    .frequency_hz = 440,
    .volume = 25,
    .frames_written = 0,
    .underruns = 0,
};

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

static void audio_task(void *arg)
{
    const int frames = 256;
    int16_t buffer[frames * 2];
    double phase = 0.0;

    ESP_LOGI(TAG, "Audio task started");

    while (true) {
        oaos_audio_state_t snapshot = oaos_audio_get_state();

        double step = 2.0 * M_PI * (double)snapshot.frequency_hz / (double)OAOS_SAMPLE_RATE;
        int amplitude = (snapshot.volume * 30000) / 100;

        for (int i = 0; i < frames; i++) {
            int16_t sample = 0;
            if (snapshot.enabled && snapshot.volume > 0) sample = (int16_t)(sin(phase) * amplitude);
            phase += step;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
            buffer[i * 2] = sample;
            buffer[i * 2 + 1] = sample;
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(tx_chan, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);

        xSemaphoreTake(audio_mutex, portMAX_DELAY);
        if (err == ESP_OK && bytes_written == sizeof(buffer)) audio_state.frames_written += frames;
        else audio_state.underruns++;
        xSemaphoreGive(audio_mutex);

        if (err != ESP_OK || bytes_written != sizeof(buffer)) {
            ESP_LOGW(TAG, "I2S write issue: err=%s bytes=%u", esp_err_to_name(err), (unsigned)bytes_written);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

esp_err_t oaos_audio_init(void)
{
    audio_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(audio_mutex != NULL, ESP_ERR_NO_MEM, TAG, "mutex failed");
    ESP_RETURN_ON_ERROR(init_i2s(), TAG, "init_i2s failed");
    xTaskCreatePinnedToCore(audio_task, "oaos_audio", 4096, NULL, 18, NULL, 1);
    ESP_LOGI(TAG, "Audio initialized");
    return ESP_OK;
}

void oaos_audio_start(void){ xSemaphoreTake(audio_mutex, portMAX_DELAY); audio_state.enabled = true; xSemaphoreGive(audio_mutex); }
void oaos_audio_stop(void){ xSemaphoreTake(audio_mutex, portMAX_DELAY); audio_state.enabled = false; xSemaphoreGive(audio_mutex); }
void oaos_audio_set_volume(int volume){ if(volume<0)volume=0; if(volume>100)volume=100; xSemaphoreTake(audio_mutex, portMAX_DELAY); audio_state.volume=volume; xSemaphoreGive(audio_mutex); }
void oaos_audio_set_frequency(int frequency_hz){ if(frequency_hz<50)frequency_hz=50; if(frequency_hz>20000)frequency_hz=20000; xSemaphoreTake(audio_mutex, portMAX_DELAY); audio_state.frequency_hz=frequency_hz; xSemaphoreGive(audio_mutex); }
oaos_audio_state_t oaos_audio_get_state(void){ oaos_audio_state_t s; xSemaphoreTake(audio_mutex, portMAX_DELAY); s=audio_state; xSemaphoreGive(audio_mutex); return s; }
