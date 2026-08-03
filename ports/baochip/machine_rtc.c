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

#include "py/runtime.h"

#include "extmod/modmachine.h"
#include "shared/timeutils/timeutils.h"

#include "hardware/rtc.h"

#if MICROPY_PY_MACHINE_RTC

typedef struct _machine_rtc_obj_t {
    mp_obj_base_t base;
} machine_rtc_obj_t;

// Singleton RTC object: the SDK's RTC is a single free-running 32-bit
// seconds counter (RTC_LR/RTC_DR), not a battery-backed calendar, so
// there's nothing per-instance to store.
static const machine_rtc_obj_t machine_rtc_obj = {{&machine_rtc_type}};

// machine.RTC()
static mp_obj_t machine_rtc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type;
    (void)args;
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    static bool started = false;
    if (!started) {
        // No battery backup: the counter always starts at 0 on power-up,
        // so datetime() reads garbage until a script sets it explicitly.
        rtc_init();
        rtc_start();
        started = true;
    }
    return MP_OBJ_FROM_PTR(&machine_rtc_obj);
}

// rtc.datetime([(year, month, mday, weekday, hour, minute, second, subsecond)])
static mp_obj_t machine_rtc_datetime(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        timeutils_struct_time_t tm;
        timeutils_seconds_since_epoch_to_struct_time(rtc_get_time(), &tm);
        mp_obj_t tuple[8] = {
            mp_obj_new_int(tm.tm_year),
            mp_obj_new_int(tm.tm_mon),
            mp_obj_new_int(tm.tm_mday),
            mp_obj_new_int(tm.tm_wday),
            mp_obj_new_int(tm.tm_hour),
            mp_obj_new_int(tm.tm_min),
            mp_obj_new_int(tm.tm_sec),
            mp_obj_new_int(0),
        };
        return mp_obj_new_tuple(8, tuple);
    }

    mp_obj_t *items;
    mp_obj_get_array_fixed_n(args[1], 8, &items);
    mp_timestamp_t seconds = timeutils_seconds_since_epoch(
        mp_obj_get_int(items[0]), mp_obj_get_int(items[1]), mp_obj_get_int(items[2]),
        mp_obj_get_int(items[4]), mp_obj_get_int(items[5]), mp_obj_get_int(items[6]));
    rtc_set_time((uint32_t)seconds);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_rtc_datetime_obj, 1, 2, machine_rtc_datetime);

static const mp_rom_map_elem_t machine_rtc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_datetime), MP_ROM_PTR(&machine_rtc_datetime_obj) },
};
static MP_DEFINE_CONST_DICT(machine_rtc_locals_dict, machine_rtc_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_rtc_type,
    MP_QSTR_RTC,
    MP_TYPE_FLAG_NONE,
    make_new, machine_rtc_make_new,
    locals_dict, &machine_rtc_locals_dict
    );

#endif // MICROPY_PY_MACHINE_RTC
