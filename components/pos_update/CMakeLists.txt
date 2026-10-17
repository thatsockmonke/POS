idf_component_register(SRCS "pos_update.c" "pos_update_defaults.c" "pos_update_transaction.c" "pos_history.c" INCLUDE_DIRS "include" REQUIRES pos_install pos_core json mbedtls)
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Werror=frame-larger-than=3072)
