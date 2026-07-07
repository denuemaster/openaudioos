#include "oaos_web.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "oaos_audio.h"
#include "oaos_storage.h"
#include "oaos_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "oaos_web";

static const char *web_page =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>OpenAudioOS M0.5</title>"
"<style>"
"body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#111;color:#eee;padding:24px;margin:0}"
".card{max-width:900px;margin:auto;background:#1d1d1f;border-radius:18px;padding:28px;box-shadow:0 20px 60px #0008}"
"input,button{box-sizing:border-box;padding:12px;margin:6px 0;border-radius:10px;border:0;font-size:16px}"
"button{background:#0a84ff;color:white;font-weight:700;cursor:pointer}button.secondary{background:#333}"
".row{display:flex;gap:10px;flex-wrap:wrap}.row>*{flex:1;min-width:160px}"
".ok{color:#5cff9d}.warn{color:#ffd36e}.muted{color:#aaa}pre{background:#101010;padding:16px;border-radius:12px;overflow:auto}a{color:#8ab4ff}"
"</style></head>"
"<body><div class='card'>"
"<h1>OpenAudioOS M0.5</h1>"
"<p class='ok'>STA-only networking and OTA partition support</p>"
"<p class='muted'>Use the IP address shown in the serial monitor or your router.</p>"
"<div class='row'>"
"<button onclick='api(\"/api/audio/start\")'>Start tone</button>"
"<button class='secondary' onclick='api(\"/api/audio/stop\")'>Stop tone</button>"
"<button class='secondary' onclick='location.href=\"/reset-wifi\"'>Reset WiFi</button>"
"</div>"
"<h2>Audio Control</h2>"
"<label>Volume <span id='volLabel'></span></label>"
"<input id='vol' type='range' min='0' max='100' value='25' oninput='setVolume(this.value)'>"
"<label>Frequency Hz</label>"
"<div class='row'>"
"<input id='freq' type='number' value='440' min='50' max='20000'>"
"<button onclick='setFreq()'>Set frequency</button>"
"</div>"
"<h2>OTA Upload</h2>"
"<input id='fw' type='file' accept='.bin'>"
"<button onclick='uploadFw()'>Upload app .bin</button>"
"<p class='warn'>Use the application binary from <code>build/OpenAudioOS.bin</code>, not a merged flash image.</p>"
"<h2>Status</h2><pre id='status'>Loading...</pre>"
"</div>"
"<script>"
"async function refresh(){let r=await fetch('/api/status');let j=await r.json();document.getElementById('status').textContent=JSON.stringify(j,null,2);document.getElementById('vol').value=j.audio.volume;document.getElementById('volLabel').textContent=j.audio.volume+'%';document.getElementById('freq').value=j.audio.frequency_hz;}"
"async function api(u){await fetch(u);refresh();}"
"async function setVolume(v){document.getElementById('volLabel').textContent=v+'%';await fetch('/api/audio/volume?value='+v);}"
"async function setFreq(){let v=document.getElementById('freq').value;await fetch('/api/audio/frequency?value='+v);refresh();}"
"async function uploadFw(){let f=document.getElementById('fw').files[0];if(!f){alert('Choose firmware .bin first');return;}let r=await fetch('/ota',{method:'POST',body:f});alert(await r.text());}"
"setInterval(refresh,15000);refresh();"
"</script></body></html>";

static const char *setup_page =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>OpenAudioOS Setup</title>"
"<style>body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#111;color:#eee;padding:32px}"
".card{max-width:620px;margin:auto;background:#1d1d1f;border-radius:18px;padding:28px}"
"input,button{width:100%;box-sizing:border-box;padding:14px;margin:8px 0;border-radius:10px;border:0;font-size:16px}"
"button{background:#0a84ff;color:white;font-weight:700}</style></head>"
"<body><div class='card'><h1>OpenAudioOS Setup</h1>"
"<p>Enter WiFi credentials. Device will reboot after saving.</p>"
"<form method='POST' action='/save'>"
"<input name='ssid' placeholder='WiFi SSID' required>"
"<input name='password' placeholder='WiFi Password' type='password'>"
"<button type='submit'>Save WiFi</button>"
"</form></div></body></html>";

static void url_decode(char *dst, const char *src, size_t dst_len)
{
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < dst_len; si++) {
        if (src[si] == '%' && src[si+1] && src[si+2]) {
            char hex[3] = { src[si+1], src[si+2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else if (src[si] == '+') dst[di++] = ' ';
        else dst[di++] = src[si];
    }
    dst[di] = 0;
}

static void form_get_value(const char *body, const char *key, char *out, size_t out_len)
{
    out[0] = 0;
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "%s=", key);
    const char *start = strstr(body, pattern);
    if (!start) return;
    start += strlen(pattern);
    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    char tmp[128] = {0};
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, start, len);
    url_decode(out, tmp, out_len);
}

static bool query_int(httpd_req_t *req, const char *key, int *out)
{
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    char value[32] = {0};
    if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) return false;
    *out = atoi(value);
    return true;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (oaos_wifi_is_configured()) return httpd_resp_send(req, web_page, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send(req, setup_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_handler(httpd_req_t *req)
{
    char body[256] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    char ssid[33] = {0};
    char password[65] = {0};
    form_get_value(body, "ssid", ssid, sizeof(ssid));
    form_get_value(body, "password", password, sizeof(password));

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(oaos_storage_save_wifi(ssid, password));
    httpd_resp_sendstr(req, "<html><body><h1>Saved</h1><p>Device will reboot now.</p></body></html>");
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
    return ESP_OK;
}

static esp_err_t reset_wifi_handler(httpd_req_t *req)
{
    oaos_storage_clear_wifi();
    httpd_resp_sendstr(req, "WiFi reset. Rebooting.");
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
    return ESP_OK;
}

static esp_err_t audio_start_handler(httpd_req_t *req){ oaos_audio_start(); httpd_resp_sendstr(req, "OK"); return ESP_OK; }
static esp_err_t audio_stop_handler(httpd_req_t *req){ oaos_audio_stop(); httpd_resp_sendstr(req, "OK"); return ESP_OK; }

static esp_err_t audio_volume_handler(httpd_req_t *req)
{
    int value = 0;
    if (!query_int(req, "value", &value)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing value");
        return ESP_FAIL;
    }
    oaos_audio_set_volume(value);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t audio_frequency_handler(httpd_req_t *req)
{
    int value = 0;
    if (!query_int(req, "value", &value)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing value");
        return ESP_FAIL;
    }
    oaos_audio_set_frequency(value);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    esp_ip4_addr_t ip = oaos_wifi_get_ip();
    oaos_audio_state_t audio = oaos_audio_get_state();
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);

    char json[1024];
    snprintf(json, sizeof(json),
             "{"
             "\"name\":\"OpenAudioOS M0.5\","
             "\"version\":\"%s\","
             "\"configured\":%s,"
             "\"has_saved_wifi\":%s,"
             "\"ssid\":\"%s\","
             "\"hostname\":\"%s\","
             "\"ip\":\"" IPSTR "\","
             "\"uptime_ms\":%lld,"
             "\"free_heap\":%lu,"
             "\"free_psram\":%lu,"
             "\"ota\":{"
                "\"running_partition\":\"%s\","
                "\"next_partition\":\"%s\","
                "\"ota_available\":%s"
             "},"
             "\"audio\":{"
                "\"enabled\":%s,"
                "\"frequency_hz\":%d,"
                "\"volume\":%d,"
                "\"frames_written\":%llu,"
                "\"underruns\":%lu,"
                "\"sample_rate\":%d,"
                "\"i2s_bclk\":%d,"
                "\"i2s_dout\":%d,"
                "\"i2s_lrck\":%d"
             "}"
             "}",
             app ? app->version : "unknown",
             oaos_wifi_is_configured() ? "true" : "false",
             oaos_wifi_has_saved_config() ? "true" : "false",
             oaos_wifi_get_ssid(),
             OAOS_HOSTNAME,
             IP2STR(&ip),
             (long long)(esp_timer_get_time() / 1000),
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             running ? running->label : "unknown",
             next ? next->label : "none",
             next ? "true" : "false",
             audio.enabled ? "true" : "false",
             audio.frequency_hz,
             audio.volume,
             (unsigned long long)audio.frames_written,
             (unsigned long)audio.underruns,
             OAOS_SAMPLE_RATE,
             OAOS_I2S_BCLK_GPIO,
             OAOS_I2S_DOUT_GPIO,
             OAOS_I2S_LRCK_GPIO);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update to partition: %s, size=%d bytes", update_partition->label, req->content_len);

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buf[2048];
    int remaining = req->content_len;
    int written = 0;

    while (remaining > 0) {
        int to_read = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            ESP_LOGE(TAG, "OTA receive failed");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        written += received;
        remaining -= received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA set boot failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA successful, written=%d. Rebooting.", written);
    httpd_resp_sendstr(req, "OTA successful. Rebooting.");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t oaos_web_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.max_uri_handlers = 12;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "httpd_start failed");

    httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=root_handler},
        {.uri="/save", .method=HTTP_POST, .handler=save_handler},
        {.uri="/reset-wifi", .method=HTTP_GET, .handler=reset_wifi_handler},
        {.uri="/api/status", .method=HTTP_GET, .handler=api_status_handler},
        {.uri="/api/audio/start", .method=HTTP_GET, .handler=audio_start_handler},
        {.uri="/api/audio/stop", .method=HTTP_GET, .handler=audio_stop_handler},
        {.uri="/api/audio/volume", .method=HTTP_GET, .handler=audio_volume_handler},
        {.uri="/api/audio/frequency", .method=HTTP_GET, .handler=audio_frequency_handler},
        {.uri="/ota", .method=HTTP_POST, .handler=ota_handler},
    };

    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &routes[i]), TAG, "register route failed");
    }

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return ESP_OK;
}
