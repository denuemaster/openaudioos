idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        oaos_system
        oaos_storage
        oaos_wifi
        oaos_web
        oaos_audio
        oaos_airplay
        oaos_airplay_adapter
)
