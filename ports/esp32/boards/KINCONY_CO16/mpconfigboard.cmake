include(boards/mpconfigboard_esp32s3_common.cmake)

list(APPEND SDKCONFIG_DEFAULTS
    boards/sdkconfig.240mhz
    boards/sdkconfig.flash_qio_80m
    boards/sdkconfig.spiram_oct
    boards/KINCONY_CO16/sdkconfig.board
)
