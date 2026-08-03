/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 MicroPython contributors
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

// This file is never compiled standalone, it's included directly from
// extmod/machine_wdt.c via MICROPY_PY_MACHINE_WDT_INCLUDEFILE.

#include "hardware/wdt.h"

// wdt_start()'s load value is timeout_ms * (WDT_CLK_HZ / 1000); the SDK's
// WDT_CLK_HZ (~11.395 MHz, empirically measured -- there's no public
// constant for it) bounds the largest timeout that fits a uint32_t.
#define WDT_TIMEOUT_MAX_MS (0xFFFFFFFFu / (11395000u / 1000u))

typedef struct _machine_wdt_obj_t {
    mp_obj_base_t base;
} machine_wdt_obj_t;

// Singleton: the SoC has a single watchdog instance.
static const machine_wdt_obj_t machine_wdt = {{&machine_wdt_type}};

static void check_timeout_ms(mp_int_t timeout_ms) {
    if (timeout_ms <= 0 || (uint32_t)timeout_ms > WDT_TIMEOUT_MAX_MS) {
        mp_raise_ValueError(MP_ERROR_TEXT("timeout out of range"));
    }
}

static machine_wdt_obj_t *mp_machine_wdt_make_new_instance(mp_int_t id, mp_int_t timeout_ms) {
    if (id != 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("WDT(%d) doesn't exist"), id);
    }
    check_timeout_ms(timeout_ms);
    wdt_start((uint32_t)timeout_ms);
    return (machine_wdt_obj_t *)&machine_wdt;
}

static void mp_machine_wdt_feed(machine_wdt_obj_t *self) {
    (void)self;
    wdt_feed();
}

#if MICROPY_PY_MACHINE_WDT_TIMEOUT_MS
static void mp_machine_wdt_timeout_ms_set(machine_wdt_obj_t *self_in, mp_int_t timeout_ms) {
    (void)self_in;
    check_timeout_ms(timeout_ms);
    // wdt_start() re-arms with a new load value; the WDT_LOCKCR
    // unlock/lock dance inside it makes this safe to call while running.
    wdt_start((uint32_t)timeout_ms);
}
#endif
