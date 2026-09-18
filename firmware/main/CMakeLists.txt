idf_component_register(SRCS "main.c" "qma7981.c" "wifi_conn.c" "http_post.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_client)