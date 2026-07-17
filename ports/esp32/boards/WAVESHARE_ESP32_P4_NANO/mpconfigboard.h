#define MICROPY_HW_BOARD_NAME               "Waveshare ESP32-P4-Nano"
#define MICROPY_HW_MCU_NAME                 "ESP32-P4"

#define MICROPY_PY_NETWORK_WLAN             (1)
#define MICROPY_PY_BLUETOOTH                (1)

// ESPNOW is not supported over the ESP-Hosted WiFi/BLE link to the onboard ESP32-C6.
#define MICROPY_PY_ESPNOW                   (0)

#define MICROPY_HW_I2C0_SCL                 (8)
#define MICROPY_HW_I2C0_SDA                 (7)

// Enable UART REPL: this board uses an external CH343 USB-UART bridge, not native USB.
#define MICROPY_HW_ENABLE_UART_REPL         (1)
