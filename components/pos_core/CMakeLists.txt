idf_component_register(SRCS "pos_core.c" "pos_settings.c" "pos_fs.c" "pos_render.c" "pos_background.c" "pos_launcher.c" "pos_editor.c" "pos_network.c"
    INCLUDE_DIRS "include" REQUIRES pos_install json mbedtls)
