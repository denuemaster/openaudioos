\
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "driver/i2s_std.h"

#define WIFI_SSID      "WiFi2"
#define WIFI_PASSWORD  "thermi555"

#define I2S_BCLK_GPIO  11
#define I2S_DOUT_GPIO  12
#define I2S_LRCK_GPIO  13

#define SAMPLE_RATE    44100
#define TONE_HZ        440
#define TONE_VOLUME    7000

static const char *TAG = "OpenAudioOS";

static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static esp_ip4_addr_t current_ip;

static i2s_chan_handle_t tx_chan = NULL;

static const char *html_page =
"<!doctype html>"
"<html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>OpenAudioOS M0</title>"
"<style>"
"body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#111;color:#eee;margin:0;padding:32px}"
".card{max-width:760px;margin:auto;background:#1d1d1f;border-radius:18px;padding:28px;box-shadow:0 20px 60px #0008}"
"h1{margin-top:0}.ok{color:#5cff9d}.muted{color:#aaa}code{background:#333;padding:2px 6px;border-radius:6px}"
"</style></head><body><div class='card'>"
"<h1>OpenAudioOS M0</h1>"
"<p class='ok'>Device online</p>"
"<p>This is the first hardware validation build.</p>"
"<ul>"
"<li>ESP32-S3</li>"
"<li>PCM5102A via I2S</li>"
"<li>GPIO11 BCK, GPIO12 DIN, GPIO13 LRCK</li>"
"<li>440 Hz test tone running</li>"
"</ul>"
"<p class='muted'>Next milestone: configurable WiFi, OTA, audio engine.</p>"
"</div></body></html>";

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started, connecting to %s", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d. Reconnecting...", disc ? disc->reason : -1);
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        current_ip = event->ip_info.ip;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&current_ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t start_wifi(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create event group");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "wifi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "ip handler failed");

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    char json[512];
    snprintf(json, sizeof(json),
             "{"
             "\"name\":\"OpenAudioOS M0\","
             "\"ip\":\"" IPSTR "\","
             "\"uptime_ms\":%lld,"
             "\"free_heap\":%lu,"
             "\"free_psram\":%lu,"
             "\"i2s_bclk\":%d,"
             "\"i2s_dout\":%d,"
             "\"i2s_lrck\":%d,"
             "\"sample_rate\":%d"
             "}",
             IP2STR(&current_ip),
             (long long)(esp_timer_get_time() / 1000),
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             I2S_BCLK_GPIO,
             I2S_DOUT_GPIO,
             I2S_LRCK_GPIO,
             SAMPLE_RATE);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "httpd_start failed");

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = NULL
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root_uri), TAG, "register root failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &status_uri), TAG, "register status failed");

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return ESP_OK;
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 512;

    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_chan, NULL), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_GPIO,
            .ws = I2S_LRCK_GPIO,
            .dout = I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_chan, &std_cfg), TAG, "i2s std init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_chan), TAG, "i2s enable failed");

    ESP_LOGI(TAG, "I2S initialized: BCLK=%d DOUT=%d LRCK=%d sample_rate=%d", I2S_BCLK_GPIO, I2S_DOUT_GPIO, I2S_LRCK_GPIO, SAMPLE_RATE);
    return ESP_OK;
}

static void audio_task(void *arg)
{
    const int frames = 256;
    int16_t buffer[frames * 2];
    double phase = 0.0;
    const double step = 2.0 * M_PI * (double)TONE_HZ / (double)SAMPLE_RATE;

    ESP_LOGI(TAG, "Audio test tone started: %d Hz", TONE_HZ);

    while (true) {
        for (int i = 0; i < frames; i++) {
            int16_t sample = (int16_t)(sin(phase) * TONE_VOLUME);
            phase += step;
            if (phase >= 2.0 * M_PI) {
                phase -= 2.0 * M_PI;
            }
            buffer[i * 2 + 0] = sample;
            buffer[i * 2 + 1] = sample;
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(tx_chan, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
        if (err != ESP_OK || bytes_written != sizeof(buffer)) {
            ESP_LOGW(TAG, "I2S write issue: err=%s bytes=%u", esp_err_to_name(err), (unsigned)bytes_written);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void status_task(void *arg)
{
    while (true) {
        ESP_LOGI(TAG, "Status: heap=%lu psram=%lu uptime=%lldms ip=" IPSTR,
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (long long)(esp_timer_get_time() / 1000),
                 IP2STR(&current_ip));
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "OpenAudioOS M0 starting");
    ESP_LOGI(TAG, "Free heap: %lu", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %lu", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(start_wifi());

    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, pdMS_TO_TICKS(30000));

    ESP_ERROR_CHECK(start_webserver());
    ESP_ERROR_CHECK(init_i2s());

    xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 20, NULL, 1);
    xTaskCreate(status_task, "status_task", 4096, NULL, 5, NULL);
}
