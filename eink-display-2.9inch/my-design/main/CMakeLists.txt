idf_component_register(SRCS "main.c"
                    REQUIRES driver esp_event esp_http_client esp_netif esp_wifi nvs_flash lwip mbedtls json esp_rom)
