idf_component_register(SRCS "main.c" "ui.c" "setup.c" "settings.c" "updates.c" "browser.c" "files.c" "background.c" "editor.c" "wifi.c" "music.c" "recorder.c" "instruments.c" "sharing.c" "expansion.c" "maps.c" "desktop.c" "calculator.c" INCLUDE_DIRS "."
    REQUIRES pos_update pos_install app_update esp_app_format fatfs sdmmc esp_driver_sdspi
             esp_driver_spi esp_driver_gpio esp_partition spi_flash mbedtls pos_core pos_device pos_recovery esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage)
target_sources(${COMPONENT_LIB} PRIVATE "meshtastic.c")
set_source_files_properties(meshtastic.c PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=3072")
if(POS_ROLE STREQUAL "installer")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE POS_INSTALLER=1)
endif()

set_source_files_properties(updates.c settings.c recorder.c instruments.c maps.c calculator.c PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=3072")
target_sources(${COMPONENT_LIB} PRIVATE "image.c" "recording_edit.c")

target_sources(${COMPONENT_LIB} PRIVATE "cable.c")
set_source_files_properties(cable.c PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=3072")
