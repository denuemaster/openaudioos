idf_component_register(
    SRCS "oaos_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_event
        esp_netif
        esp_wifi
        oaos_storage
)
