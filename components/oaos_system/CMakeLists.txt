idf_component_register(
    SRCS "oaos_system.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_timer
        esp_psram
)
