set(SOURCES
    "app_main.c"
    "non_volatile_storage.c"
    "led.c"
    "wifi.c"
    "ds18b20.c"
    "temperature.c"
    "mqtt.c"
    "task_monitor.c"
)

set(INCLUDES "." "include")

idf_component_register(SRCS ${SOURCES} INCLUDE_DIRS ${INCLUDES})

set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Werror")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
