/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020
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

#include "py/mpconfig.h"
#if MICROPY_PY_MACHINE_PIN

#include <stdio.h>
#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "extmod/virtpin.h"
#include "extmod/machine_pin.h"

// Pin class

typedef struct _mp_machine_pin_obj_t {
    mp_obj_base_t base;
} mp_machine_pin_obj_t;

// pin.init(mode, pull=None, *, value)
STATIC mp_obj_t machine_pin_init_helper(const mp_machine_pin_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    (void)self;
    enum { ARG_mode, ARG_pull, ARG_value };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode, MP_ARG_OBJ, {.u_obj = mp_const_none}},
        { MP_QSTR_pull, MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_SMALL_INT(-1)}},
        { MP_QSTR_value, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    return mp_const_none;
}

STATIC mp_obj_t machine_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_machine_pin_obj_t *self = m_new_obj(mp_machine_pin_obj_t);
    self->base.type = &machine_pin_type;

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    machine_pin_init_helper(self, n_args - 1, args + 1, &kw_args);

    return MP_OBJ_FROM_PTR(self);
}

// fast method for getting/setting pin value
STATIC mp_obj_t machine_pin_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    mp_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)self;
    if (n_args == 0) {
        // get pin
        printf("get pin\n");
        return MP_OBJ_NEW_SMALL_INT(0);
    } else {
        // set pin
        mp_hal_pin_write(0, mp_obj_is_true(args[0]));
        return mp_const_none;
    }
}

// pin.init(mode, pull)
STATIC mp_obj_t machine_pin_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return machine_pin_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_init_obj, 1, machine_pin_init);

// pin.value([value])
STATIC mp_obj_t machine_pin_value(size_t n_args, const mp_obj_t *args) {
    return machine_pin_call(args[0], n_args - 1, 0, args + 1);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_pin_value_obj, 1, 2, machine_pin_value);

// pin.off()
STATIC mp_obj_t machine_pin_off(mp_obj_t self_in) {
    mp_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)self;
    mp_hal_pin_low(0);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_off_obj, machine_pin_off);

// pin.on()
STATIC mp_obj_t machine_pin_on(mp_obj_t self_in) {
    mp_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)self;
    mp_hal_pin_high(0);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_on_obj, machine_pin_on);

STATIC const mp_rom_map_elem_t machine_pin_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_pin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&machine_pin_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&machine_pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&machine_pin_on_obj) },
    //{ MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&machine_pin_irq_obj) },

    // // class constants
    // { MP_ROM_QSTR(MP_QSTR_IN), MP_ROM_INT(GPIO_MODE_INPUT) },
    // { MP_ROM_QSTR(MP_QSTR_OUT), MP_ROM_INT(GPIO_MODE_INPUT_OUTPUT) },
    // { MP_ROM_QSTR(MP_QSTR_OPEN_DRAIN), MP_ROM_INT(GPIO_MODE_INPUT_OUTPUT_OD) },
    // { MP_ROM_QSTR(MP_QSTR_PULL_UP), MP_ROM_INT(GPIO_PULL_UP) },
    // { MP_ROM_QSTR(MP_QSTR_PULL_DOWN), MP_ROM_INT(GPIO_PULL_DOWN) },
    // { MP_ROM_QSTR(MP_QSTR_PULL_HOLD), MP_ROM_INT(GPIO_PULL_HOLD) },
    // { MP_ROM_QSTR(MP_QSTR_IRQ_RISING), MP_ROM_INT(GPIO_PIN_INTR_POSEDGE) },
    // { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING), MP_ROM_INT(GPIO_PIN_INTR_NEGEDGE) },
    // { MP_ROM_QSTR(MP_QSTR_WAKE_LOW), MP_ROM_INT(GPIO_PIN_INTR_LOLEVEL) },
    // { MP_ROM_QSTR(MP_QSTR_WAKE_HIGH), MP_ROM_INT(GPIO_PIN_INTR_HILEVEL) },
};

STATIC MP_DEFINE_CONST_DICT(machine_pin_locals_dict, machine_pin_locals_dict_table);

const mp_obj_type_t machine_pin_type = {
    { &mp_type_type },
    .name = MP_QSTR_Pin,
    .make_new = machine_pin_make_new,
    .call = machine_pin_call,
    .locals_dict = (mp_obj_t)&machine_pin_locals_dict,
};

#endif // MICROPY_PY_MACHINE_PIN
