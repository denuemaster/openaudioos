idf_component_register(
    SRCS "oaos_airplay.c"
    INCLUDE_DIRS "include"
    REQUIRES
        oaos_audio
        esp_timer
        mdns
        lwip
)
