idf_component_register(
    SRCS "oaos_airplay_backend.c"
    INCLUDE_DIRS "include"
    REQUIRES
        oaos_airplay_adapter
        oaos_audio
)
