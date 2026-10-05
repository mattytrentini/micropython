#define MICROPY_HW_BOARD_NAME               "KinCony CO16"
#define MICROPY_HW_MCU_NAME                 "ESP32-S3"

// Use native USB for the REPL; GPIO43/44 are connected to the W5500.
#define MICROPY_HW_ENABLE_UART_REPL         (0)

#define MICROPY_HW_I2C0_SCL                 (18)
#define MICROPY_HW_I2C0_SDA                 (8)

// W5500 Ethernet has a separate SPI bus.
#define MICROPY_HW_SPI1_MOSI                (2)
#define MICROPY_HW_SPI1_MISO                (41)
#define MICROPY_HW_SPI1_SCK                 (1)

// Shared SPI bus: display, SD card, MAX31865 and optional LoRa module.
// SDCard(slot=2) uses SPI3_HOST, the same host as SPI(2).
#define MICROPY_HW_SPI2_MOSI                (10)
#define MICROPY_HW_SPI2_MISO                (12)
#define MICROPY_HW_SPI2_SCK                 (11)

#define MICROPY_HW_SDCARD_SPI_MOSI          (10)
#define MICROPY_HW_SDCARD_SPI_MISO          (12)
#define MICROPY_HW_SDCARD_SPI_SCK           (11)
#define MICROPY_HW_SDCARD_SPI_CS            (9)

// Optional SIM7600/KNX module, RS485 and Tuya module, respectively.
#define MICROPY_HW_UART0_TX                 (48)
#define MICROPY_HW_UART0_RX                 (47)
#define MICROPY_HW_UART1_TX                 (39)
#define MICROPY_HW_UART1_RX                 (38)
#define MICROPY_HW_UART2_TX                 (16)
#define MICROPY_HW_UART2_RX                 (17)
