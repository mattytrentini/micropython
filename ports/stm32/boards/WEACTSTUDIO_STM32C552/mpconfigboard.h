#define MICROPY_HW_BOARD_NAME               "WeAct Studio STM32C552"
#define MICROPY_HW_MCU_NAME                 "STM32C552VE"

#define MICROPY_PY_PYB_LEGACY               (0)
#define MICROPY_PY_MACHINE_MEM_BACKUP       (1)
#define MICROPY_HW_HAS_FLASH                (1)
#define MICROPY_HW_ENABLE_INTERNAL_FLASH_STORAGE (1)
#define MICROPY_HW_HAS_SWITCH               (1)
#define MICROPY_HW_ENABLE_RTC               (1)
#define MICROPY_HW_RTC_USE_LSE              (1)
#define MICROPY_HW_RTC_LSE_DRIVE            (LL_RCC_LSEDRIVE_MEDIUMHIGH)
#define MICROPY_HW_ENABLE_RNG               (1)
#define MICROPY_HW_ENABLE_ADC               (0)
#define MICROPY_HW_ENABLE_DAC               (0)
#define MICROPY_HW_ENABLE_I2S               (1)
#define MICROPY_HW_I2S1                     (1)

// 8 MHz HSE crystal, with PSI generating a 144 MHz system clock.
#define MICROPY_HW_CLK_USE_HSE              (1)
#define MICROPY_HW_CLK_PSI_REF              (HAL_RCC_PSI_REF_8MHZ)
#define MICROPY_HW_CLK_PSI_OUT              (HAL_RCC_PSI_OUT_144MHZ)
#define MICROPY_HW_FLASH_LATENCY            FLASH_LATENCY_4

// Native USB CDC REPL without USB MSC or a resident application bootloader.
#define MICROPY_HW_ENABLE_USB               (1)
#define MICROPY_HW_TINYUSB_STACK            (1)
#define MICROPY_HW_ENABLE_USB_RUNTIME_DEVICE (1)
#define MICROPY_HW_USB_FS                   (1)
#define MICROPY_HW_USB_MSC                  (0)
#define MICROPY_HW_USB_PRODUCT_FS_STRING    "WeAct STM32C552"

// USART1 is also available as a diagnostic REPL on the P5 TX/RX header.
#define MICROPY_HW_UART1_TX                 (pin_A9)
#define MICROPY_HW_UART1_RX                 (pin_A10)
#define MICROPY_HW_UART2_TX                 (pin_A2)
#define MICROPY_HW_UART2_RX                 (pin_A3)
#define MICROPY_HW_UART3_TX                 (pin_D8)
#define MICROPY_HW_UART3_RX                 (pin_D9)
#define MICROPY_HW_UART4_TX                 (pin_A0)
#define MICROPY_HW_UART4_RX                 (pin_A1)
#define MICROPY_HW_UART5_TX                 (pin_C12)
#define MICROPY_HW_UART5_RX                 (pin_D2)
#define MICROPY_HW_UART_REPL                PYB_UART_1
#define MICROPY_HW_UART_REPL_BAUD           115200

#define MICROPY_HW_SPI1_NSS                 (pin_A4)
#define MICROPY_HW_SPI1_SCK                 (pin_A5)
#define MICROPY_HW_SPI1_MISO                (pin_A6)
#define MICROPY_HW_SPI1_MOSI                (pin_A7)
#define MICROPY_HW_SPI2_NSS                 (pin_B12)
#define MICROPY_HW_SPI2_SCK                 (pin_B13)
#define MICROPY_HW_SPI2_MISO                (pin_B14)
#define MICROPY_HW_SPI2_MOSI                (pin_B15)
#define MICROPY_HW_SPI3_NSS                 (pin_A15)
#define MICROPY_HW_SPI3_SCK                 (pin_B3)
#define MICROPY_HW_SPI3_MISO                (pin_B4)
#define MICROPY_HW_SPI3_MOSI                (pin_B5)

#define MICROPY_HW_I2C1_SCL                 (pin_B6)
#define MICROPY_HW_I2C1_SDA                 (pin_B7)
#define MICROPY_HW_I2C2_SCL                 (pin_B10)
#define MICROPY_HW_I2C2_SDA                 (pin_B12)

// FDCAN1 in Classical CAN mode; an external transceiver is required.
#define MICROPY_HW_CAN1_TX                  (pin_D1)
#define MICROPY_HW_CAN1_RX                  (pin_D0)

// PC13 has an external pulldown; K3 connects it to 3.3 V when pressed.
#define MICROPY_HW_USRSW_PIN                (pin_C13)
#define MICROPY_HW_USRSW_PULL               GPIO_NOPULL
#define MICROPY_HW_USRSW_EXTI_MODE          GPIO_MODE_IT_RISING
#define MICROPY_HW_USRSW_PRESSED            (1)

// Blue user LED D1 on PH15, active high.
#define MICROPY_HW_LED1                     (pin_H15)
#define MICROPY_HW_LED_ON(pin)              (mp_hal_pin_high(pin))
#define MICROPY_HW_LED_OFF(pin)             (mp_hal_pin_low(pin))
