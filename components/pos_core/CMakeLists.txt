idf_component_register(SRCS "pos_core.c" "pos_settings.c" "pos_fs.c" "pos_render.c" "pos_background.c" "pos_launcher.c" "pos_editor.c" "pos_network.c"
    INCLUDE_DIRS "include" REQUIRES pos_install json mbedtls)
# Editor/network operations nest inside the 16-KiB UI task. Keep large I/O
# buffers on the heap and fail compilation if a stack-heavy frame returns.
set_source_files_properties(pos_editor.c pos_network.c PROPERTIES
    COMPILE_OPTIONS "-fstack-usage;-Werror=frame-larger-than=3072")
