idf_component_register(SRCS "pos_device.c" "pos_health.c" "pos_update_service.c" "pos_console.c" "pos_network_service.c" "pos_known_networks.c" "pos_audio_device.c" "pos_instrument_device.c" "pos_expansion.c" "pos_ble.c" "vendor/sx126x/sx126x.c" INCLUDE_DIRS "include"
    REQUIRES app_update pos_update pos_core esp_app_format esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio esp_driver_ledc
             esp_adc esp_timer esp_hw_support esp_wifi esp_netif esp_event nvs_flash esp_http_client esp_http_server mbedtls esp_driver_i2s esp_driver_uart esp_driver_rmt bt)
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
