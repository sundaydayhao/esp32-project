idf_component_register(
    SRCS "main.c" "anomaly_detector.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer driver
)