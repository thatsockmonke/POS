idf_component_register(SRCS "pos_device.c" "pos_console.c" "pos_network_service.c" INCLUDE_DIRS "include"
    REQUIRES pos_core esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio esp_driver_ledc
             esp_adc esp_timer esp_hw_support esp_wifi esp_netif esp_event nvs_flash esp_http_client esp_http_server mbedtls)
