#include "oaos_wifi.h"

#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "oaos_storage.h"

static const char *TAG = "oaos_wifi";

static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

static bool sta_configured = false;
static bool sta_connected = false;
static char saved_ssid[33] = {0};
static char saved_password[65] = {0};
static esp_ip4_addr_t current_ip = {0};

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started, connecting to %s", saved_ssid);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d. Reconnecting...", disc ? disc->reason : -1);
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        current_ip.addr = 0;
        sta_connected = false;
        if (sta_configured) esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "Setup AP started: %s / password: %s", OAOS_SETUP_AP_SSID, OAOS_SETUP_AP_PASSWORD);
        ESP_LOGI(TAG, "Open http://192.168.4.1");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        current_ip = event->ip_info.ip;
        sta_connected = true;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&current_ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t oaos_wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group failed");

    if (oaos_storage_load_wifi(saved_ssid, sizeof(saved_ssid), saved_password, sizeof(saved_password)) == ESP_OK) {
        sta_configured = true;
    } else {
        ESP_LOGW(TAG, "No WiFi config found, setup AP only");
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta_netif, OAOS_HOSTNAME);

    if (!sta_configured) {
        esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "wifi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "ip handler failed");

    if (sta_configured) {
        wifi_config_t sta_config = {0};
        strlcpy((char *)sta_config.sta.ssid, saved_ssid, sizeof(sta_config.sta.ssid));
        strlcpy((char *)sta_config.sta.password, saved_password, sizeof(sta_config.sta.password));

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "set STA config failed");
        ESP_LOGI(TAG, "Starting WiFi STA-only mode");
    } else {
        wifi_config_t ap_config = {0};
        strlcpy((char *)ap_config.ap.ssid, OAOS_SETUP_AP_SSID, sizeof(ap_config.ap.ssid));
        strlcpy((char *)ap_config.ap.password, OAOS_SETUP_AP_PASSWORD, sizeof(ap_config.ap.password));
        ap_config.ap.ssid_len = strlen(OAOS_SETUP_AP_SSID);
        ap_config.ap.channel = 1;
        ap_config.ap.max_connection = 4;
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set AP config failed");
        ESP_LOGI(TAG, "Starting WiFi setup AP-only mode");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    return ESP_OK;
}

bool oaos_wifi_is_configured(void)
{
    return sta_connected && current_ip.addr != 0;
}

bool oaos_wifi_has_saved_config(void)
{
    return sta_configured;
}

const char *oaos_wifi_get_ssid(void)
{
    return saved_ssid;
}

esp_ip4_addr_t oaos_wifi_get_ip(void)
{
    return current_ip;
}
