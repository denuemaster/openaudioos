#include "oaos_system.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "oaos_system";

static void system_status_task(void *arg)
{
    while (true) {
        oaos_system_print_status();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

esp_err_t oaos_system_init(void)
{
    ESP_LOGI(TAG, "System init");
    oaos_system_print_status();
    xTaskCreate(system_status_task, "oaos_system_status", 4096, NULL, 3, NULL);
    return ESP_OK;
}

void oaos_system_print_status(void)
{
    ESP_LOGI(TAG, "heap=%lu psram=%lu uptime=%lldms",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (long long)(esp_timer_get_time() / 1000));
}
