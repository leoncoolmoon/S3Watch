idf_component_register(
    SRCS
        main.cpp
    INCLUDE_DIRS "."
    REQUIRES boot_manager
)

## enable the next line to upload the spiffs content
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)