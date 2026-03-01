idf_component_register(SRCS "firebase.c"
                    PRIV_REQUIRES esp-tls https_client
                    INCLUDE_DIRS ".")