idf_component_register(SRCS "main.c"
							"rest_server.c"
							"rtc.c"
							"74AHC595.c"
							"esp_rest_main.c"
							"mqtt_com.c"
							"ota.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)
                   
