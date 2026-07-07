#include "oaos_storage.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "oaos_storage";
static const char *NVS_NS = "oaos";

esp_err_t oaos_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs_flash_init failed");
    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}

esp_err_t oaos_storage_load_wifi(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_get_str(nvs, "ssid", ssid, &ssid_len);
    if (err == ESP_OK) err = nvs_get_str(nvs, "pass", password, &password_len);
    nvs_close(nvs);

    if (err == ESP_OK && strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Loaded WiFi config: %s", ssid);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t oaos_storage_save_wifi(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &nvs), TAG, "nvs_open failed");
    ESP_RETURN_ON_ERROR(nvs_set_str(nvs, "ssid", ssid), TAG, "nvs_set ssid failed");
    ESP_RETURN_ON_ERROR(nvs_set_str(nvs, "pass", password ? password : ""), TAG, "nvs_set pass failed");
    ESP_RETURN_ON_ERROR(nvs_commit(nvs), TAG, "nvs_commit failed");
    nvs_close(nvs);
    ESP_LOGI(TAG, "Saved WiFi config: %s", ssid);
    return ESP_OK;
}

esp_err_t oaos_storage_clear_wifi(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    nvs_erase_key(nvs, "ssid");
    nvs_erase_key(nvs, "pass");
    err = nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Cleared WiFi config");
    return err;
}
