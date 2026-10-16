idf_component_register(SRCS "main.c" "ui.c" "setup.c" "browser.c" "background.c" "desktop.c" INCLUDE_DIRS "."
    REQUIRES pos_install app_update esp_app_format fatfs sdmmc esp_driver_sdspi
             esp_driver_spi esp_driver_gpio esp_partition spi_flash mbedtls pos_core pos_device pos_recovery esp_timer)
if(POS_ROLE STREQUAL "installer")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE POS_INSTALLER=1)
endif()
