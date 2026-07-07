#include "oaos_airplay.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>

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
#include "oaos_airplay_adapter.h"

static const char *TAG = "oaos_airplay";

#define AIRPLAY_PLACEHOLDER_CHUNK_FRAMES 256
#define AIRPLAY_RTSP_PORT 5000
#define RTSP_RX_BUFFER_SIZE 8192
#define RTSP_RESPONSE_BUFFER_SIZE 4096

static SemaphoreHandle_t ap_mutex;
static bool enabled = true;
static bool placeholder_streaming = false;
static bool mdns_started = false;
static bool raop_advertised = false;
static bool airplay_advertised = false;
static bool rtsp_listener_started = false;
static oaos_airplay_state_t state = OAOS_AIRPLAY_STATE_IDLE;
static uint64_t start_time_us = 0;

static uint32_t sessions_started = 0;
static uint32_t rtsp_connections = 0;
static uint32_t rtsp_requests = 0;
static uint32_t rtsp_options = 0;
static uint32_t rtsp_info = 0;
static uint32_t rtsp_fp_setup = 0;
static uint32_t rtsp_pair_setup = 0;
static uint32_t rtsp_pair_verify = 0;
static uint32_t rtsp_announce = 0;
static uint32_t rtsp_setup = 0;
static uint32_t rtsp_record = 0;
static uint32_t rtsp_teardown = 0;
static uint32_t packets_received = 0;
static uint32_t frames_pushed = 0;
static uint32_t errors = 0;
static uint32_t last_content_length = 0;
static uint8_t last_fp_header[16] = {0};

static char raop_instance[64] = "OpenAudioOS";
static char device_name[32] = "OpenAudioOS";
static char device_id[18] = "00:00:00:00:00:00";
static char device_id_compact[13] = "000000000000";

const char *oaos_airplay_state_name(oaos_airplay_state_t s)
{
    switch (s) {
        case OAOS_AIRPLAY_STATE_DISABLED: return "disabled";
        case OAOS_AIRPLAY_STATE_IDLE: return "idle";
        case OAOS_AIRPLAY_STATE_ADVERTISING: return "advertising";
        case OAOS_AIRPLAY_STATE_CONNECTED: return "connected";
        case OAOS_AIRPLAY_STATE_FP_SETUP: return "fp_setup";
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

static void build_device_ids(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(device_id, sizeof(device_id), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(device_id_compact, sizeof(device_id_compact), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(raop_instance, sizeof(raop_instance), "%s@%s", device_id_compact, device_name);
}

static esp_err_t start_mdns_airplay(void)
{
    if (mdns_started) return ESP_OK;

    build_device_ids();

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
        {"vs", "220.68"},
        {"am", "AudioAccessory6,1"},
        {"sf", "0x4"},
        {"pk", "0000000000000000000000000000000000000000000000000000000000000000"}
    };

    err = mdns_service_add(raop_instance, "_raop", "_tcp", AIRPLAY_RTSP_PORT,
                           raop_txt, sizeof(raop_txt) / sizeof(raop_txt[0]));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS RAOP service add failed: %s", esp_err_to_name(err));
        return err;
    }
    raop_advertised = true;

    mdns_txt_item_t airplay_txt[] = {
        {"deviceid", device_id},
        {"features", "0x5A7FFFF7,0x1E"},
        {"flags", "0x4"},
        {"model", "AudioAccessory6,1"},
        {"manufacturer", "OpenAudioOS"},
        {"serialNumber", device_id_compact},
        {"srcvers", "220.68"},
        {"protovers", "1.1"},
        {"vv", "2"},
        {"pi", "00000000-0000-0000-0000-000000000000"},
        {"pk", "0000000000000000000000000000000000000000000000000000000000000000"}
    };

    err = mdns_service_add(device_name, "_airplay", "_tcp", AIRPLAY_RTSP_PORT,
                           airplay_txt, sizeof(airplay_txt) / sizeof(airplay_txt[0]));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS AirPlay service add failed: %s", esp_err_to_name(err));
        return err;
    }
    airplay_advertised = true;

    mdns_started = true;

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    if (enabled && state == OAOS_AIRPLAY_STATE_IDLE) set_state_locked(OAOS_AIRPLAY_STATE_ADVERTISING);
    xSemaphoreGive(ap_mutex);

    ESP_LOGI(TAG, "RAOP mDNS advertised as '%s' on port %d", raop_instance, AIRPLAY_RTSP_PORT);
    ESP_LOGI(TAG, "AirPlay mDNS advertised as '%s' deviceid=%s on port %d", device_name, device_id, AIRPLAY_RTSP_PORT);
    ESP_LOGW(TAG, "M0.15 internal simple AirPlay: discovery + RTSP + fp-setup diagnostics only; no real FairPlay keys yet.");
    return ESP_OK;
}

static const char *find_header_case(const char *request, const char *header)
{
    size_t header_len = strlen(header);
    const char *p = request;
    while (*p) {
        if (strncasecmp(p, header, header_len) == 0) return p + header_len;
        const char *next = strstr(p, "\n");
        if (!next) break;
        p = next + 1;
    }
    return NULL;
}

static int get_content_length(const char *request)
{
    const char *p = find_header_case(request, "Content-Length:");
    if (!p) return 0;
    while (*p == ' ' || *p == '\t') p++;
    return atoi(p);
}

static const uint8_t *get_body_ptr(const char *request, int total_len, int *body_len)
{
    const char *p = strstr(request, "\r\n\r\n");
    if (!p) {
        *body_len = 0;
        return NULL;
    }

    p += 4;
    int header_len = (int)(p - request);
    if (header_len > total_len) {
        *body_len = 0;
        return NULL;
    }

    *body_len = total_len - header_len;
    return (const uint8_t *)p;
}

static void get_cseq(const char *request, char *out, size_t out_len)
{
    const char *p = find_header_case(request, "CSeq:");
    if (!p) {
        strlcpy(out, "1", out_len);
        return;
    }
    while (*p == ' ' || *p == '\t') p++;
    size_t i = 0;
    while (*p && *p != '\r' && *p != '\n' && i + 1 < out_len) out[i++] = *p++;
    out[i] = 0;
}

static void get_method(const char *request, char *out, size_t out_len)
{
    size_t i = 0;
    while (request[i] && request[i] != ' ' && request[i] != '\r' && request[i] != '\n' && i + 1 < out_len) {
        out[i] = request[i];
        i++;
    }
    out[i] = 0;
}

static void get_path(const char *request, char *out, size_t out_len)
{
    const char *p = strchr(request, ' ');
    if (!p) {
        strlcpy(out, "/", out_len);
        return;
    }
    p++;
    size_t i = 0;
    while (p[i] && p[i] != ' ' && p[i] != '\r' && p[i] != '\n' && i + 1 < out_len) {
        out[i] = p[i];
        i++;
    }
    out[i] = 0;
}

static void send_rtsp_response(int client, const char *cseq, const char *code, const char *headers, const uint8_t *body, size_t body_len)
{
    if (!headers) headers = "";

    char *response = heap_caps_malloc(RTSP_RESPONSE_BUFFER_SIZE, MALLOC_CAP_8BIT);
    if (!response) {
        ESP_LOGE(TAG, "No memory for RTSP response");
        return;
    }

    int len = snprintf(response, RTSP_RESPONSE_BUFFER_SIZE,
             "RTSP/1.0 %s\r\n"
             "CSeq: %s\r\n"
             "Server: OpenAudioOS-M0.15\r\n"
             "%s"
             "Content-Length: %u\r\n"
             "\r\n",
             code,
             cseq,
             headers,
             (unsigned)body_len);

    if (len > 0) {
        send(client, response, strlen(response), 0);
        if (body && body_len > 0) {
            send(client, body, body_len, 0);
        }
    }
    free(response);
}

static void send_rtsp_text(int client, const char *cseq, const char *code, const char *text)
{
    if (!text) text = "";
    send_rtsp_response(client, cseq, code, "Content-Type: text/plain\r\n", (const uint8_t *)text, strlen(text));
}

static void log_first_bytes(const char *label, const uint8_t *body, int body_len)
{
    int n = body_len < 32 ? body_len : 32;
    char hex[128] = {0};
    size_t pos = 0;
    for (int i = 0; i < n && pos + 4 < sizeof(hex); i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", body[i]);
    }
    ESP_LOGI(TAG, "%s: content_length=%d first_bytes=%s", label, body_len, hex);
}

static void handle_fp_setup(int client, const char *cseq, const uint8_t *body, int body_len)
{
    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    rtsp_fp_setup++;
    last_content_length = body_len;
    memset(last_fp_header, 0, sizeof(last_fp_header));
    if (body && body_len > 0) {
        memcpy(last_fp_header, body, body_len < 16 ? body_len : 16);
    }
    set_state_locked(OAOS_AIRPLAY_STATE_FP_SETUP);
    xSemaphoreGive(ap_mutex);

    log_first_bytes("FP-SETUP summary", body, body_len);

    /*
     * Important:
     * This is deliberately NOT a fake FairPlay response.
     * A real response requires the correct FairPlay key exchange.
     * We return a clean RTSP error so the ESP stays stable and diagnostics continue.
     */
    send_rtsp_text(client, cseq, "501 Not Implemented",
                   "OpenAudioOS internal AirPlay reached /fp-setup. Real FairPlay response is not implemented yet.\n");
}

static void handle_rtsp_request(int client, const char *rx, int rx_len)
{
    char method[32] = {0};
    char path[96] = {0};
    char cseq[32] = {0};
    get_method(rx, method, sizeof(method));
    get_path(rx, path, sizeof(path));
    get_cseq(rx, cseq, sizeof(cseq));

    int body_len = 0;
    const uint8_t *body = get_body_ptr(rx, rx_len, &body_len);
    int advertised_len = get_content_length(rx);
    if (advertised_len > body_len) {
        ESP_LOGW(TAG, "RTSP body incomplete in single recv: advertised=%d got=%d", advertised_len, body_len);
    }

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    rtsp_requests++;
    xSemaphoreGive(ap_mutex);

    ESP_LOGI(TAG, "RTSP method=%s path=%s cseq=%s body=%d advertised=%d", method, path, cseq, body_len, advertised_len);

    if (strcasecmp(method, "OPTIONS") == 0) {
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        rtsp_options++;
        xSemaphoreGive(ap_mutex);

        send_rtsp_response(client, cseq, "200 OK",
                           "Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER, POST\r\n",
                           NULL, 0);
        return;
    }

    if (strcasecmp(method, "POST") == 0 && strcmp(path, "/fp-setup") == 0) {
        handle_fp_setup(client, cseq, body, body_len);
        return;
    }

    if (strcasecmp(method, "POST") == 0 && strcmp(path, "/pair-setup") == 0) {
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        rtsp_pair_setup++;
        xSemaphoreGive(ap_mutex);
        log_first_bytes("PAIR-SETUP summary", body, body_len);
        send_rtsp_text(client, cseq, "501 Not Implemented", "pair-setup not implemented yet.\n");
        return;
    }

    if (strcasecmp(method, "POST") == 0 && strcmp(path, "/pair-verify") == 0) {
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        rtsp_pair_verify++;
        xSemaphoreGive(ap_mutex);
        log_first_bytes("PAIR-VERIFY summary", body, body_len);
        send_rtsp_text(client, cseq, "501 Not Implemented", "pair-verify not implemented yet.\n");
        return;
    }

    if (strcasecmp(method, "GET") == 0 || strcmp(path, "/info") == 0 || strcasecmp(method, "GET_PARAMETER") == 0) {
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        rtsp_info++;
        xSemaphoreGive(ap_mutex);

        char info[768];
        snprintf(info, sizeof(info),
                 "{"
                 "\"name\":\"%s\","
                 "\"model\":\"AudioAccessory6,1\","
                 "\"manufacturer\":\"OpenAudioOS\","
                 "\"deviceid\":\"%s\","
                 "\"features\":\"0x5A7FFFF7,0x1E\","
                 "\"srcvers\":\"220.68\","
                 "\"statusFlags\":\"0x4\","
                 "\"vv\":2,"
                 "\"internal\":\"simple-airplay-m0.15\""
                 "}\n",
                 device_name,
                 device_id);

        send_rtsp_response(client, cseq, "200 OK", "Content-Type: application/json\r\n", (const uint8_t *)info, strlen(info));
        return;
    }

    if (strcasecmp(method, "ANNOUNCE") == 0) {
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        rtsp_announce++;
        xSemaphoreGive(ap_mutex);
        send_rtsp_text(client, cseq, "501 Not Implemented", "ANNOUNCE not implemented yet.\n");
        return;
    }

    if (strcasecmp(method, "SETUP") == 0) {
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        rtsp_setup++;
        xSemaphoreGive(ap_mutex);
        send_rtsp_text(client, cseq, "501 Not Implemented", "SETUP not implemented yet.\n");
        return;
    }

    if (strcasecmp(method, "RECORD") == 0) {
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        rtsp_record++;
        xSemaphoreGive(ap_mutex);
        send_rtsp_text(client, cseq, "501 Not Implemented", "RECORD not implemented yet.\n");
        return;
    }

    if (strcasecmp(method, "TEARDOWN") == 0) {
        xSemaphoreTake(ap_mutex, portMAX_DELAY);
        rtsp_teardown++;
        xSemaphoreGive(ap_mutex);
        send_rtsp_response(client, cseq, "200 OK", NULL, NULL, 0);
        return;
    }

    send_rtsp_text(client, cseq, "501 Not Implemented", "RTSP method/path not implemented yet.\n");
}

static void rtsp_client_task(void *arg)
{
    int client = (int)(intptr_t)arg;

    char *rx = heap_caps_malloc(RTSP_RX_BUFFER_SIZE, MALLOC_CAP_8BIT);
    if (!rx) {
        ESP_LOGE(TAG, "No memory for RTSP RX buffer");
        close(client);
        vTaskDelete(NULL);
        return;
    }

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    rtsp_connections++;
    set_state_locked(OAOS_AIRPLAY_STATE_CONNECTED);
    xSemaphoreGive(ap_mutex);

    while (true) {
        int len = recv(client, rx, RTSP_RX_BUFFER_SIZE - 1, 0);
        if (len <= 0) break;

        rx[len] = 0;

        char first_line[160] = {0};
        const char *eol = strstr(rx, "\r\n");
        size_t first_len = eol ? (size_t)(eol - rx) : (size_t)len;
        if (first_len >= sizeof(first_line)) first_len = sizeof(first_line) - 1;
        memcpy(first_line, rx, first_len);

        ESP_LOGI(TAG, "RTSP request: %s", first_line);
        handle_rtsp_request(client, rx, len);
    }

    free(rx);
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

    ESP_LOGI(TAG, "RTSP listener started on TCP port %d", AIRPLAY_RTSP_PORT);

    while (true) {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int client = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (client < 0) {
            ESP_LOGW(TAG, "RTSP accept failed: errno=%d", errno);
            continue;
        }

        xTaskCreate(rtsp_client_task, "oaos_rtsp_client", 12288, (void *)(intptr_t)client, 6, NULL);
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
        else if (enabled && mdns_started && state != OAOS_AIRPLAY_STATE_FP_SETUP) set_state_locked(OAOS_AIRPLAY_STATE_ADVERTISING);
        else if (enabled && state != OAOS_AIRPLAY_STATE_FP_SETUP) set_state_locked(OAOS_AIRPLAY_STATE_IDLE);
        else if (!enabled) set_state_locked(OAOS_AIRPLAY_STATE_DISABLED);
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

        esp_err_t err = oaos_airplay_adapter_push_pcm_s16_stereo(
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

    ESP_LOGI(TAG, "OpenAudioOS internal simple AirPlay M0.15 initialized");
    ESP_LOGW(TAG, "M0.15 is our own implementation path. It reaches /fp-setup but does not implement FairPlay keys yet.");

    esp_err_t err = start_mdns_airplay();
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
    oaos_airplay_adapter_claim_source();

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

    oaos_airplay_adapter_release_source();
    return ESP_OK;
}

oaos_airplay_status_t oaos_airplay_get_status(void)
{
    oaos_airplay_status_t s;

    xSemaphoreTake(ap_mutex, portMAX_DELAY);
    s.enabled = enabled;
    s.mdns_started = mdns_started;
    s.raop_advertised = raop_advertised;
    s.airplay_advertised = airplay_advertised;
    s.rtsp_listener_started = rtsp_listener_started;
    s.rtsp_port = AIRPLAY_RTSP_PORT;
    s.state = state;
    s.state_name = oaos_airplay_state_name(state);
    s.uptime_ms = (esp_timer_get_time() - start_time_us) / 1000;
    s.sessions_started = sessions_started;
    s.rtsp_connections = rtsp_connections;
    s.rtsp_requests = rtsp_requests;
    s.rtsp_options = rtsp_options;
    s.rtsp_info = rtsp_info;
    s.rtsp_fp_setup = rtsp_fp_setup;
    s.rtsp_pair_setup = rtsp_pair_setup;
    s.rtsp_pair_verify = rtsp_pair_verify;
    s.rtsp_announce = rtsp_announce;
    s.rtsp_setup = rtsp_setup;
    s.rtsp_record = rtsp_record;
    s.rtsp_teardown = rtsp_teardown;
    s.packets_received = packets_received;
    s.frames_pushed = frames_pushed;
    s.errors = errors;
    s.last_content_length = last_content_length;
    memcpy(s.last_fp_header, last_fp_header, sizeof(s.last_fp_header));
    s.device_name = device_name;
    s.raop_instance = raop_instance;
    s.protocol_note = "M0.15: internal simple AirPlay path; discovery + RTSP + fp-setup diagnostics; no FairPlay keys yet";
    xSemaphoreGive(ap_mutex);

    return s;
}
