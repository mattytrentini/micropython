/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef MICROPY_INCLUDED_SAMD_MPHALPORT_H
#define MICROPY_INCLUDED_SAMD_MPHALPORT_H

#include <stdint.h>

#ifdef assert
#undef assert
#endif
#define assert samd_assert
#include "include/hal_gpio.h"
#include "samd/pins.h"
#undef assert
#include <assert.h>

extern volatile uint32_t systick_ms;

void mp_hal_set_interrupt_char(int c);

static inline mp_uint_t mp_hal_ticks_ms(void) {
    return systick_ms;
}
static inline mp_uint_t mp_hal_ticks_us(void) {
    return systick_ms * 1000;
}
static inline mp_uint_t mp_hal_ticks_cpu(void) {
    return 0;
}

#define mp_hal_pin_obj_t int

#define mp_hal_pin_write(p, v)  ((v) ? mp_hal_pin_high(p) : mp_hal_pin_low(p))

static inline void mp_hal_pin_low(mp_hal_pin_obj_t pin) {
    (void)pin;
    // See https://github.com/adafruit/circuitpython/blob/master/ports/atmel-samd/common-hal/digitalio/DigitalInOut.c#L96
    const mcu_pin_obj_t *p = &pin_PA10;
    gpio_set_pin_direction(p->number, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(p->number, false);
}

static inline void mp_hal_pin_high(mp_hal_pin_obj_t pin) {
    (void)pin;
    // See https://github.com/adafruit/circuitpython/blob/master/ports/atmel-samd/common-hal/digitalio/DigitalInOut.c#L96
    const mcu_pin_obj_t *p = &pin_PA10;
    gpio_set_pin_direction(p->number, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(p->number, true);
}


#endif // MICROPY_INCLUDED_SAMD_MPHALPORT_H
