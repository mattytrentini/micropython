set(IDF_TARGET esp32s3)

# Provide different variants for the downloads page
set(BOARD_VARIANTS "n8 n8r2 n8r8 n16r8v n32r8v")

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.usb
    boards/sdkconfig.ble
    boards/sdkconfig.240mhz
)

if("${BOARD_VARIANT}" STREQUAL "n8" OR ${BOARD_VARIANT} NOT EXISTS)
    set(SDKCONFIG_DEFAULTS
        ${SDKCONFIG_DEFAULTS}
        boards/ESP32_S3_DEVKITC_1/sdkconfig_8mb.board
    )
endif()

if("${BOARD_VARIANT}" STREQUAL "n8r2")
    set(SDKCONFIG_DEFAULTS
        ${SDKCONFIG_DEFAULTS}
        boards/sdkconfig.spiram_sx
        boards/ESP32_S3_DEVKITC_1/sdkconfig_8mb.board
    )
endif()

if("${BOARD_VARIANT}" STREQUAL "n8r8")
    set(SDKCONFIG_DEFAULTS
        ${SDKCONFIG_DEFAULTS}
        boards/sdkconfig.spiram_sx
        boards/sdkconfig.spiram_oct
        boards/ESP32_S3_DEVKITC_1/sdkconfig_8mb.board
    )
endif()

if("${BOARD_VARIANT}" STREQUAL "n16r8v")
    set(SDKCONFIG_DEFAULTS
        ${SDKCONFIG_DEFAULTS}
        boards/sdkconfig.spiram_sx
        boards/sdkconfig.spiram_oct
        boards/ESP32_S3_DEVKITC_1/sdkconfig_16mb.board
    )
endif()

if("${BOARD_VARIANT}" STREQUAL "n32r8v")
    # NOTE: This uses the 16mb config as 32mb is not yet supported.
    # Should be updated to 32mb once Espressif build chain supports it.
    set(SDKCONFIG_DEFAULTS
        ${SDKCONFIG_DEFAULTS}
        boards/sdkconfig.spiram_sx
        boards/sdkconfig.spiram_oct
        boards/ESP32_S3_DEVKITC_1/sdkconfig_16mb.board  # To be updated
    )
endif()
