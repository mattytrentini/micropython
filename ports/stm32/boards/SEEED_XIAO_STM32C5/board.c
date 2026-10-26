
#include "py/mphal.h"

void board_early_init(void) {
    // Keep the IMU heater and battery measurement circuit off.
    mp_hal_gpio_clock_enable(pin_A8->gpio);
    mp_hal_pin_low(pin_A8);
    mp_hal_pin_output(pin_A8);
    mp_hal_pin_low(pin_A15);
    mp_hal_pin_output(pin_A15);

    // Leave the onboard CAN transceiver in standby until explicitly used.
    mp_hal_gpio_clock_enable(pin_B14->gpio);
    mp_hal_pin_high(pin_B14);
    mp_hal_pin_output(pin_B14);
}

void board_enter_bootloader(void) {
    // Factory TinyUF2 checks the last SRAM word for its double-reset marker.
    // This is outside the application heap/stack. Storage is flushed by the
    // caller before this transient request overwrites the unused cache tail.
    *(volatile uint32_t *)0x2003fffc = 0xf01669ef;
    NVIC_SystemReset();
}
