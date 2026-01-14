idf_component_register(SRCS "main.c"
							"rest_server.c"
							"rtc.c"
							"74AHC595.c"
							"esp_rest_main.c"
							"mqtt_com.c"
							"ota.c"
							"led.c"
							"config.c"
							"ble_scoreboard.c"
							"ble_debug.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)
                   
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../front")
if(NOT EXISTS "${WEB_SRC_DIR}/dist")
    message(FATAL_ERROR "${WEB_SRC_DIR}/dist doesn't exist. Please run 'npm run build' in ${WEB_SRC_DIR}")
endif()
spiffs_create_partition_image(www_0 "${WEB_SRC_DIR}/dist" FLASH_IN_PROJECT)