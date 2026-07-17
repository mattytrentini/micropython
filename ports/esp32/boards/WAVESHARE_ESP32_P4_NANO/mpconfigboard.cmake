include(boards/mpconfigboard_esp32p4_common.cmake)

list(APPEND SDKCONFIG_DEFAULTS
    boards/sdkconfig.p4_wifi_common
    boards/sdkconfig.p4_wifi_c6
)
