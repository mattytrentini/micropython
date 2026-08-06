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
// extmod/machine_adc.c via MICROPY_PY_MACHINE_ADC_INCLUDEFILE.

#include "hardware/adc.h"
#include "hardware/gpio.h"

#include "machine_pin.h"

// The SDK's adc_init() unconditionally muxes PC9 as the sole external
// analog input; channels 1-3 exist in the VINSEL mux but aren't wired
// to any Dabao pin, so the only usable ADC objects are PC9 (channel 0)
// and the internal temperature sensor.
#define ADC_EXT_CHANNEL (0)

#define MICROPY_PY_MACHINE_ADC_CLASS_CONSTANTS \
    { MP_ROM_QSTR(MP_QSTR_CORE_TEMP), MP_ROM_INT(-1) },

typedef struct _machine_adc_obj_t {
    mp_obj_base_t base;
    bool is_temp;
} machine_adc_obj_t;

static machine_adc_obj_t machine_adc_obj[2]; // [0] = PC9, [1] = CORE_TEMP
static bool machine_adc_initialised = false;

static void mp_machine_adc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "ADC(%s)", self->is_temp ? "CORE_TEMP" : "PC9");
}

// machine.ADC(pin) or machine.ADC(machine.ADC.CORE_TEMP)
static mp_obj_t mp_machine_adc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type;
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    bool is_temp = mp_obj_is_int(args[0]) && mp_obj_get_int(args[0]) == -1;
    if (!is_temp) {
        const machine_pin_obj_t *pin = machine_pin_find(args[0]);
        if (pin->port != GPIO_PORT_C || pin->pin != 9) {
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("ADC(%q) is not wired up"), pin->name);
        }
    }

    if (!machine_adc_initialised) {
        adc_init();
        machine_adc_initialised = true;
    }

    machine_adc_obj_t *self = &machine_adc_obj[is_temp ? 1 : 0];
    self->base.type = &machine_adc_type;
    self->is_temp = is_temp;
    return MP_OBJ_FROM_PTR(self);
}

static uint32_t mp_machine_adc_read_raw(machine_adc_obj_t *self) {
    return self->is_temp ? adc_read_temp_raw() : adc_read_raw(ADC_EXT_CHANNEL);
}

// read_u16()
static mp_int_t mp_machine_adc_read_u16(machine_adc_obj_t *self) {
    uint32_t raw = mp_machine_adc_read_raw(self);
    // Scale the 10-bit raw reading to 16 bits (Taylor expansion, valid for 8-16 bits).
    return (raw << (16 - ADC_RESOLUTION)) | (raw >> (2 * ADC_RESOLUTION - 16));
}

// read_uv()
static mp_int_t mp_machine_adc_read_uv(machine_adc_obj_t *self) {
    uint32_t raw = mp_machine_adc_read_raw(self);
    return (mp_int_t)adc_raw_to_mv(raw) * 1000;
}
