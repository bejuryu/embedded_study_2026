set(SRCS main.c
        system_init.c
        system_init.h
        wifi.c
        wifi.h
)
set(REQUIRES)

idf_component_register(SRCS ${SRCS}
        INCLUDE_DIRS "."
        REQUIRES ${REQUIRES}
)
