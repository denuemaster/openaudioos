idf_component_register(
    SRCS "oaos_web.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_server
        esp_timer
        app_update
        esp_app_format
        oaos_audio
        oaos_storage
        oaos_wifi
)
