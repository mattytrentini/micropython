#define MICROPY_HW_BOARD_NAME               "01Space ESP32S3 OLED"
#define MICROPY_HW_MCU_NAME                 "ESP32S3"

#define MICROPY_PY_MACHINE_DAC      (0)

// Enable UART REPL for modules that have an external USB-UART and don't use native USB.
#define MICROPY_HW_ENABLE_UART_REPL (1)

// I2C0 for the STEMMA QT connector
#define MICROPY_HW_I2C0_SCL         (1)
#define MICROPY_HW_I2C0_SDA         (2)

// I2C1 for the OLED Display
#define MICROPY_HW_I2C1_SCL         (40)
#define MICROPY_HW_I2C1_SDA         (41)
