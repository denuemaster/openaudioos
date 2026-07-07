#include "oaos_airplay.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mdns.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "oaos_audio.h"

static const char *TAG = "oaos_airplay";

#define AIRPLAY_PLACEHOLDER_CHUNK_FRAMES 256
#define AIRPLAY_RTSP_PORT 5000

static SemaphoreHandle_t ap_mutex;
static bool enabled = true;
static bool placeholder_streaming = false;
static bool mdns_started = false;
static bool rtsp_listener_started = false;
static oaos_airplay_state_t state = OAOS_AIRPLAY_STATE_IDLE;
static uint64_t start_time_us = 0;
static uint32_t sessions_started = 0;
static uint32_t rtsp_connections = 0;
static uint32_t rtsp_requests = 0;
static uint32_t packets_received = 0;
static uint32_t frames_pushed = 0;
static uint32_t errors = 0;
static char raop_instance[64] = "OpenAudioOS";
static char device_name[32] = "OpenAudioOS";

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

static void build_raop_instance_name(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(raop_instance, sizeof(raop_instance),
             "%02X%02X%02X%02X%02X%02X@%s",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             device_name);
}

static esp_err_t start_mdns_raop(void)
{
    if (mdns_started) {
        return ESP_OK;
    }

    build_raop_instance_name();

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return err;
    }

    mdns_hostname_set("openaudioos");
    mdns_instance_name_set(device_name);

    mdns_txt_item_t raop_txt[] = {
        {"txtvers", "1"},
        {"ch", "2"},
        {"cn", "0,1,2,3"},
        {"da", "true"},
        {"et", "0,1"},
        {"ft", "0x5A7FFFF7,0x1E"},
        {"md", "0,1,2"},
        {"pw", "false"},
        {"sr", "44100"},
        {"ss", "16"},
        {"sv", "false"},
        {"tp", "UDP"},
        {"vn", "65537"},
        {"vs", "130.14"},
        {"am", "OpenAudioOS"},
        {"sf", "0x4"}
    };

    err = mdns_service_add(raop_instance, "_raop", "_tcp", AIRPLAY_RTSP_PORT,
                           raop_txt, sizeof(raop_txt) / sizeof(raop_txt[0]));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS RAOP service add failed: %s", esp_err_to_name(err));
        return err;
    }

    mdns_started = true;

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    if (enabled && state == OAOS_AIRPLAY_STATE_IDLE) {
        set_state_locked(OAOS_AIRPLAY_STATE_ADVERTISING);
    }
    xSemaphoreGive(ap_mutex);

    ESP_LOGI(TAG, "RAOP mDNS advertised as '%s' on port %d", raop_instance, AIRPLAY_RTSP_PORT);
    ESP_LOGW(TAG, "M0.9 only advertises/listens. Full RTSP/RAOP auth + audio is not implemented yet.");

    return ESP_OK;
}

static void get_cseq(const char *request, char *out, size_t out_len)
{
    const char *p = strcasestr(request, "CSeq:");
    if (!p) {
        strlcpy(out, "1", out_len);
        return;
    }

    p += 5;
    while (*p == ' ' || *p == '\t') p++;

    size_t i = 0;
    while (*p && *p != '\r' && *p != '\n' && i + 1 < out_len) {
        out[i++] = *p++;
    }
    out[i] = 0;
}

static void rtsp_client_task(void *arg)
{
    int client = (int)(intptr_t)arg;
    char rx[1024];

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    rtsp_connections++;
    set_state_locked(OAOS_AIRPLAY_STATE_CONNECTED);
    xSemaphoreGive(ap_mutex);

    int len = recv(client, rx, sizeof(rx) - 1, 0);
    if (len > 0) {
        rx[len] = 0;
        char cseq[32];
        get_cseq(rx, cseq, sizeof(cseq));

        char first_line[128] = {0};
        const char *eol = strstr(rx, "\r\n");
        size_t first_len = eol ? (size_t)(eol - rx) : (size_t)len;
        if (first_len >= sizeof(first_line)) first_len = sizeof(first_line) - 1;
        memcpy(first_line, rx, first_len);

        ESP_LOGI(TAG, "RTSP request: %s", first_line);

        const char *body =
            "OpenAudioOS M0.9 RAOP listener is alive, but full RAOP is not implemented yet.\n";

        char response[512];
        snprintf(response, sizeof(response),
                 "RTSP/1.0 501 Not Implemented\r\n"
                 "CSeq: %s\r\n"
                 "Server: OpenAudioOS-M0.9\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %u\r\n"
                 "\r\n"
                 "%s",
                 cseq,
                 (unsigned)strlen(body),
                 body);

        send(client, response, strlen(response), 0);

        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        rtsp_requests++;
        xSemaphoreGive(ap_mutex);
    }

    shutdown(client, 0);
    close(client);

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    if (enabled && mdns_started) set_state_locked(OAOS_AIRPLAY_STATE_ADVERTISING);
    else if (enabled) set_state_locked(OAOS_AIRPLAY_STATE_IDLE);
    xSemaphoreGive(ap_mutex);

    vTaskDelete(NULL);
}

static void rtsp_listener_task(void *arg)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create RTSP socket: errno=%d", errno);
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        errors++;
        set_state_locked(OAOS_AIRPLAY_STATE_ERROR);
        xSemaphoreGive(ap_mutex);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(AIRPLAY_RTSP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "RTSP bind failed: errno=%d", errno);
        close(listen_sock);
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        errors++;
        set_state_locked(OAOS_AIRPLAY_STATE_ERROR);
        xSemaphoreGive(ap_mutex);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 4) != 0) {
        ESP_LOGE(TAG, "RTSP listen failed: errno=%d", errno);
        close(listen_sock);
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        errors++;
        set_state_locked(OAOS_AIRPLAY_STATE_ERROR);
        xSemaphoreGive(ap_mutex);
        vTaskDelete(NULL);
        return;
    }

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    rtsp_listener_started = true;
    if (enabled && mdns_started) set_state_locked(OAOS_AIRPLAY_STATE_ADVERTISING);
    xSemaphoreGive(ap_mutex);

    ESP_LOGI(TAG, "RTSP placeholder listener started on TCP port %d", AIRPLAY_RTSP_PORT);

    while (true) {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int client = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (client < 0) {
            ESP_LOGW(TAG, "RTSP accept failed: errno=%d", errno);
            continue;
        }

        xTaskCreate(rtsp_client_task, "oaos_rtsp_client", 4096, (void *)(intptr_t)client, 6, NULL);
    }
}

static void airplay_placeholder_task(void *arg)
{
    int16_t buffer[AIRPLAY_PLACEHOLDER_CHUNK_FRAMES * OAOS_CHANNELS];
    double phase = 0.0;

    ESP_LOGI(TAG, "AirPlay placeholder producer started");

    while (true) {
        bool run = false;

        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        run = enabled && placeholder_streaming;
        if (run) set_state_locked(OAOS_AIRPLAY_STATE_STREAMING);
        else if (enabled && mdns_started) set_state_locked(OAOS_AIRPLAY_STATE_ADVERTISING);
        else if (enabled) set_state_locked(OAOS_AIRPLAY_STATE_IDLE);
        else set_state_locked(OAOS_AIRPLAY_STATE_DISABLED);
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
        if (err == ESP_OK) frames_pushed += AIRPLAY_PLACEHOLDER_CHUNK_FRAMES;
        else errors++;
        xSemaphoreGive(ap_mutex);

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t oaos_airplay_init(void)
{
    ap_mutex = xSemaphoreCreateMutex();
    if (!ap_mutex) return ESP_ERR_NO_MEM;

    start_time_us = esp_timer_get_time();

    ESP_LOGI(TAG, "AirPlay/RAOP foundation initialized");
    ESP_LOGW(TAG, "M0.9 advertises RAOP and listens on RTSP port 5000, but it is not a playable AirPlay receiver yet.");

    esp_err_t err = start_mdns_raop();
    if (err != ESP_OK) {
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        errors++;
        set_state_locked(OAOS_AIRPLAY_STATE_ERROR);
        xSemaphoreGive(ap_mutex);
    }

    xTaskCreatePinnedToCore(rtsp_listener_task, "oaos_rtsp_listen", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(airplay_placeholder_task, "oaos_airplay_ph", 4096, NULL, 7, NULL, 0);
    return ESP_OK;
}

esp_err_t oaos_airplay_enable(void)
{
    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    enabled = true;
    if (mdns_started) set_state_locked(OAOS_AIRPLAY_STATE_ADVERTISING);
    else set_state_locked(OAOS_AIRPLAY_STATE_IDLE);
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
    if (!placeholder_streaming) sessions_started++;
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
    if (enabled && mdns_started) set_state_locked(OAOS_AIRPLAY_STATE_ADVERTISING);
    else set_state_locked(enabled ? OAOS_AIRPLAY_STATE_IDLE : OAOS_AIRPLAY_STATE_DISABLED);
    xSemaphoreGive(ap_mutex);
    return ESP_OK;
}

oaos_airplay_status_t oaos_airplay_get_status(void)
{
    oaos_airplay_status_t s;

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    s.enabled = enabled;
    s.mdns_started = mdns_started;
    s.rtsp_listener_started = rtsp_listener_started;
    s.rtsp_port = AIRPLAY_RTSP_PORT;
    s.state = state;
    s.state_name = oaos_airplay_state_name(state);
    s.uptime_ms = (esp_timer_get_time() - start_time_us) / 1000;
    s.sessions_started = sessions_started;
    s.rtsp_connections = rtsp_connections;
    s.rtsp_requests = rtsp_requests;
    s.packets_received = packets_received;
    s.frames_pushed = frames_pushed;
    s.errors = errors;
    s.device_name = device_name;
    s.protocol_note = "M0.9: RAOP mDNS advertisement and RTSP placeholder listener only; no full AirPlay playback yet";
    xSemaphoreGive(ap_mutex);

    return s;
}
