idf_component_register(SRCS "pos_core.c" "pos_settings.c" "pos_fs.c" "pos_render.c" "pos_background.c" "pos_launcher.c" "pos_editor.c" "pos_network.c" "pos_files.c" "pos_share.c" "pos_location.c" "pos_maps.c" "pos_navigation.c" "pos_radio.c" "pos_music.c"
    INCLUDE_DIRS "include" REQUIRES pos_install json mbedtls)
target_sources(${COMPONENT_LIB} PRIVATE "pos_visual.c" "pos_capabilities.c" "pos_battery.c" "pos_timezone.c" "pos_diagnostics.c" "pos_recorder.c" "pos_instruments.c" "pos_appdata.c" "pos_calculator.c")
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage)
target_sources(${COMPONENT_LIB} PRIVATE "pos_mesh.c")
idf_component_optional_requires(PRIVATE esp_hw_support)
set_source_files_properties(pos_mesh.c PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=3072")
# Editor/network operations nest inside the 16-KiB UI task. Keep large I/O
# buffers on the heap and fail compilation if a stack-heavy frame returns.
set_source_files_properties(pos_editor.c pos_network.c PROPERTIES
    COMPILE_OPTIONS "-fstack-usage;-Werror=frame-larger-than=3072")
set_source_files_properties(pos_music.c PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=3072")

set_source_files_properties(pos_recorder.c pos_appdata.c pos_instruments.c PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=3072")

set_source_files_properties(pos_maps.c pos_navigation.c PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=3072")

target_sources(${COMPONENT_LIB} PRIVATE "pos_calc_tools.c")

target_sources(${COMPONENT_LIB} PRIVATE "pos_chat.c")

set_source_files_properties(pos_calc_tools.c pos_chat.c PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=3072")
target_sources(${COMPONENT_LIB} PRIVATE "pos_file_tools.c")
target_sources(${COMPONENT_LIB} PRIVATE "pos_image.c")
target_sources(${COMPONENT_LIB} PRIVATE "pos_wave.c")
target_sources(${COMPONENT_LIB} PRIVATE "pos_graph.c")
target_sources(${COMPONENT_LIB} PRIVATE "pos_mesh_cache.c")
