#define MICROPY_HW_BOARD_NAME               "NUCLEO_C5A3ZG"
#define MICROPY_HW_MCU_NAME                 "STM32C5A3ZG"

#define MICROPY_PY_PYB_LEGACY               (0)
#define MICROPY_HW_ENABLE_INTERNAL_FLASH_STORAGE (0)
#define MICROPY_PY_MACHINE_MEM_BACKUP       (1)
#define MICROPY_HW_ENABLE_RTC               (1)
// The Nucleo-64 board includes a 32.768 kHz LSE crystal.
#define MICROPY_HW_RTC_USE_LSE              (1)
#define MICROPY_HW_RTC_LSE_DRIVE           (LL_RCC_LSEDRIVE_MEDIUMHIGH)
#define MICROPY_HW_ENABLE_RNG               (1)
#define MICROPY_HW_I2S1                      (1)
#define MICROPY_HW_ENABLE_ADC               (0) // TODO: enable once ADC driver adapted
#define MICROPY_HW_ENABLE_DAC               (0) // TODO: enable once DAC driver adapted
#define MICROPY_HW_ENABLE_USB               (1)
#define MICROPY_HW_TINYUSB_STACK            (1)
#define MICROPY_HW_ENABLE_USB_RUNTIME_DEVICE (1)
#define MICROPY_HW_USB_FS                   (1)
#define MICROPY_HW_USB_MSC                  (0)
#define MICROPY_HW_USB_PRODUCT_FS_STRING    "NUCLEO-C5"
#define MICROPY_HW_HAS_SWITCH               (1)
#define MICROPY_HW_HAS_FLASH                (0)

// The board has a 48MHz HSE oscillator, use PSI to get 144MHz SYSCLK
// MICROPY_HW_CLK_VALUE is set from HSE_VALUE by mpconfigboard_common.h
#define MICROPY_HW_CLK_USE_HSE              (1)
#define MICROPY_HW_CLK_PSI_REF              (HAL_RCC_PSI_REF_48MHZ)
#define MICROPY_HW_CLK_PSI_OUT              (HAL_RCC_PSI_OUT_144MHZ)

// 4 wait states for 144 MHz at VOS range 1
#define MICROPY_HW_FLASH_LATENCY            FLASH_LATENCY_4


// UART config
// USART2 on PA2/PA3 is the ST-Link VCP on NUCLEO-C5A3ZG
#define MICROPY_HW_UART2_TX                 (pin_A2)
#define MICROPY_HW_UART2_RX                 (pin_A3)
// Also define USART3 on PD8/PD9
#define MICROPY_HW_UART3_TX                 (pin_D8)
#define MICROPY_HW_UART3_RX                 (pin_D9)

// SPI3 on PB3/PB4/PB5.
#define MICROPY_HW_SPI3_SCK                 (pin_B3)
#define MICROPY_HW_SPI3_MISO                (pin_B4)
#define MICROPY_HW_SPI3_MOSI                (pin_B5)

#define MICROPY_HW_I2C1_SCL                  (pin_B6)
#define MICROPY_HW_I2C1_SDA                  (pin_B7)
#define MICROPY_HW_I2C2_SCL                  (pin_B3)
#define MICROPY_HW_I2C2_SDA                  (pin_B4)
// Connect REPL to UART2 which is provided on ST-Link USB interface
#define MICROPY_HW_UART_REPL                PYB_UART_2
#define MICROPY_HW_UART_REPL_BAUD           115200

// USRSW is active high (directly mapped to PC13, active-high)
#define MICROPY_HW_USRSW_PIN                (pin_C13)
#define MICROPY_HW_USRSW_PULL               (GPIO_NOPULL)
#define MICROPY_HW_USRSW_EXTI_MODE          (GPIO_MODE_IT_RISING)
#define MICROPY_HW_USRSW_PRESSED            (1)

// LEDs
#define MICROPY_HW_LED1                     (pin_A5) // Green (LD2)
#define MICROPY_HW_LED_ON(pin)              (mp_hal_pin_high(pin))
#define MICROPY_HW_LED_OFF(pin)             (mp_hal_pin_low(pin))
