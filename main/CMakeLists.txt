idf_component_register(SRCS "main.c"
							"rest_server.c"
							"rtc.c"
							"74AHC595.c"
							"esp_rest_main.c"
                    INCLUDE_DIRS ".")
                   
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../front")
if(NOT EXISTS "${WEB_SRC_DIR}/dist")
    message(FATAL_ERROR "${WEB_SRC_DIR}/dist doesn't exist. Please run 'npm run build' in ${WEB_SRC_DIR}")
endif()
spiffs_create_partition_image(www "${WEB_SRC_DIR}/dist" FLASH_IN_PROJECT)