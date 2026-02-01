idf_component_register(
    SRCS "esp32_spp_vss.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bt nvs_flash driver esp_timer
)
