idf_component_register(
    SRCS "main.c" "nn.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer
)