#define MICROPY_HW_BOARD_NAME               "M5Stack Dial"
#define MICROPY_HW_MCU_NAME                 "ESP32-S3"
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT "M5StackDial"

// Port A I2C
#define MICROPY_HW_I2C0_SCL (15)
#define MICROPY_HW_I2C0_SDA (13)

// Internal I2C, for RTC and RFID chips
#define MICROPY_HW_I2C1_SCL (12)
#define MICROPY_HW_I2C1_SDA (11)

#define MICROPY_HW_SPI1_NAME            "SPI1"
#define MICROPY_HW_SPI1_SCK             (6)
#define MICROPY_HW_SPI1_MOSI            (5)
#define MICROPY_HW_SPI1_MISO            (16) // Dummy pin
