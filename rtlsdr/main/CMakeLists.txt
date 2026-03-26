idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "rtlsdr_usb.c"
        "web_server.c"
        "adsb/adsb_decoder.c"
        "adsb/adsb_rmt.c"
        "fm/fm_receiver.c"
        "spectrum/spectrum_analyzer.c"
        "ais/ais_decoder.c"
        "drone/drone_detector.c"
    INCLUDE_DIRS
        "."
        "adsb"
        "fm"
        "spectrum"
        "ais"
        "drone"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_http_server
        spiffs
        driver
        esp_timer
        json
        usb
)

# Create SPIFFS image from spiffs_image directory
spiffs_create_partition_image(spiffs ../spiffs_image FLASH_IN_PROJECT)