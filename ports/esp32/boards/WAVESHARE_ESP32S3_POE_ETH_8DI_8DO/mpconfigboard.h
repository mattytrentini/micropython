#define MICROPY_HW_BOARD_NAME       "Waveshare ESP32-S3-POE-ETH-8DI-8DO"
#define MICROPY_HW_MCU_NAME         "ESP32S3"
#define MICROPY_HW_ENABLE_UART_REPL (1)

// I2C0 - RTC (PCF85063) and IO expander (TCA9554PWR)
#define MICROPY_HW_I2C0_SCL        (41)
#define MICROPY_HW_I2C0_SDA        (42)

// SPI2 - W5500 Ethernet
#define MICROPY_HW_SPI2_MOSI       (13)
#define MICROPY_HW_SPI2_MISO       (14)
#define MICROPY_HW_SPI2_SCK        (15)
