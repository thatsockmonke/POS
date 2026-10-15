idf_component_register(SRCS "pos_backup.c" "pos_reinstall.c" "pos_backup_set.c" "pos_migrate.c" INCLUDE_DIRS "include"
    REQUIRES pos_core pos_install json)
