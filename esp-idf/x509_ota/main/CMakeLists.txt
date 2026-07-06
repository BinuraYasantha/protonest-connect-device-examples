idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_update esp_driver_gpio esp_event esp_http_client esp_https_ota esp_netif esp_wifi json mqtt nvs_flash)
