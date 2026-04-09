idf_component_register(SRCS "main.c"
                              "wifi_csi.c"
                              "motion_gate.c"
                              "event_detector.c"
                              "data_uplink.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi nvs_flash esp_event esp_netif)