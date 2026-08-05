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
// extmod/machine_pwm.c via MICROPY_PY_MACHINE_PWM_INCLUDEFILE.

#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "machine_pin.h"

// The SDK wires PWM as two fixed 4-channel slices, one per GPIO bank:
// slice 1 = PB0..PB3 (channel = pin number), slice 2 = PC0..PC3 likewise.
// All 4 channels on a slice share one frequency (one counter/period), so
// this file has to track state per *slice*, not just per channel:
// pwm_init() resets the whole slice and zeroes every channel's duty as a
// side effect, so changing one channel's frequency would silently kill a
// sibling channel's duty unless we save and re-apply it here.
#define PWM_BANK_SLICE_B (1)
#define PWM_BANK_SLICE_C (2)
#define PWM_NUM_SLICES   (3) // slots 0 (unused), 1, 2 -- keeps slice usable as a direct index

typedef struct _machine_pwm_obj_t {
    mp_obj_base_t base;
    uint8_t slice;
    uint8_t channel;
} machine_pwm_obj_t;

static machine_pwm_obj_t machine_pwm_obj[2][4]; // [slice-1][channel]

// 0 means "this slice has never been initialised"; freq_hz is always > 0.
static uint32_t pwm_slice_freq[PWM_NUM_SLICES];
static bool pwm_slice_channel_active[PWM_NUM_SLICES][4];
static uint16_t pwm_slice_channel_duty_u16[PWM_NUM_SLICES][4];

static void mp_machine_pwm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "PWM(slice=%u, channel=%u, freq=%u)",
        self->slice, self->channel, pwm_slice_freq[self->slice]);
}

// Re-init the slice at a (possibly new) frequency, then restore every
// other active channel's duty -- pwm_init() unconditionally zeroes all
// 4 channels, so anything already running on this slice must be
// re-applied afterwards or it silently drops to 0%.
static void pwm_slice_set_freq(uint8_t slice, uint32_t freq_hz) {
    pwm_init(slice, freq_hz);
    pwm_slice_freq[slice] = freq_hz;
    for (uint8_t ch = 0; ch < 4; ++ch) {
        if (pwm_slice_channel_active[slice][ch]) {
            pwm_set_percent(slice, ch, (uint32_t)pwm_slice_channel_duty_u16[slice][ch] * 100 / 65535);
        }
    }
}

static void mp_machine_pwm_freq_set(machine_pwm_obj_t *self, mp_int_t freq) {
    if (freq <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("freq must be > 0"));
    }
    if ((uint32_t)freq != pwm_slice_freq[self->slice]) {
        // All 4 channels on this slice share one frequency.
        pwm_slice_set_freq(self->slice, (uint32_t)freq);
    }
}

static mp_obj_t mp_machine_pwm_freq_get(machine_pwm_obj_t *self) {
    return mp_obj_new_int_from_uint(pwm_slice_freq[self->slice]);
}

static void mp_machine_pwm_duty_set_u16(machine_pwm_obj_t *self, mp_int_t duty_u16) {
    if (pwm_slice_freq[self->slice] == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("PWM frequency not set"));
    }
    if (duty_u16 < 0) {
        duty_u16 = 0;
    } else if (duty_u16 > 65535) {
        duty_u16 = 65535;
    }
    pwm_slice_channel_active[self->slice][self->channel] = true;
    pwm_slice_channel_duty_u16[self->slice][self->channel] = (uint16_t)duty_u16;
    pwm_set_percent(self->slice, self->channel, (uint32_t)duty_u16 * 100 / 65535);
}

static mp_obj_t mp_machine_pwm_duty_get_u16(machine_pwm_obj_t *self) {
    if (!pwm_slice_channel_active[self->slice][self->channel]) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    return mp_obj_new_int_from_uint(pwm_slice_channel_duty_u16[self->slice][self->channel]);
}

// duty_ns is a thin wrapper over duty_u16, scaled by the slice's current
// period -- the SDK has no direct nanosecond API, and re-deriving the
// counter/prescaler math independently of pwm_set_percent() would just
// be two implementations of the same thing to keep in sync.
static void mp_machine_pwm_duty_set_ns(machine_pwm_obj_t *self, mp_int_t duty_ns) {
    if (pwm_slice_freq[self->slice] == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("PWM frequency not set"));
    }
    if (duty_ns < 0) {
        duty_ns = 0;
    }
    uint64_t period_ns = 1000000000ULL / pwm_slice_freq[self->slice];
    uint32_t duty_u16 = period_ns == 0 ? 65535 :
        (uint32_t)MIN(65535ULL, ((uint64_t)duty_ns * 65535ULL) / period_ns);
    mp_machine_pwm_duty_set_u16(self, duty_u16);
}

static mp_obj_t mp_machine_pwm_duty_get_ns(machine_pwm_obj_t *self) {
    if (pwm_slice_freq[self->slice] == 0 || !pwm_slice_channel_active[self->slice][self->channel]) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    uint64_t period_ns = 1000000000ULL / pwm_slice_freq[self->slice];
    uint32_t duty_u16 = pwm_slice_channel_duty_u16[self->slice][self->channel];
    return mp_obj_new_int_from_uint((duty_u16 * period_ns) / 65535);
}

static void mp_machine_pwm_init_helper(machine_pwm_obj_t *self,
    size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_freq, ARG_duty_u16, ARG_duty_ns };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_duty_u16, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_duty_ns,  MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_freq].u_int >= 0) {
        mp_machine_pwm_freq_set(self, args[ARG_freq].u_int);
    }
    if (args[ARG_duty_u16].u_int >= 0) {
        mp_machine_pwm_duty_set_u16(self, args[ARG_duty_u16].u_int);
    } else if (args[ARG_duty_ns].u_int >= 0) {
        mp_machine_pwm_duty_set_ns(self, args[ARG_duty_ns].u_int);
    }
}

// machine.PWM(pin, *, freq, duty_u16, duty_ns)
static mp_obj_t mp_machine_pwm_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type;
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    const machine_pin_obj_t *pin = machine_pin_find(args[0]);

    uint8_t slice;
    if (pin->port == GPIO_PORT_B && pin->pin <= 3) {
        slice = PWM_BANK_SLICE_B;
    } else if (pin->port == GPIO_PORT_C && pin->pin <= 3) {
        slice = PWM_BANK_SLICE_C;
    } else {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("PWM(%q) is not wired up"), pin->name);
    }
    uint8_t channel = pin->pin;

    machine_pwm_obj_t *self = &machine_pwm_obj[slice - 1][channel];
    self->base.type = &machine_pwm_type;
    self->slice = slice;
    self->channel = channel;

    pwm_init_pin(pin->port, pin->pin);

    if (n_args > 1 || n_kw > 0) {
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        mp_machine_pwm_init_helper(self, n_args - 1, args + 1, &kw_args);
    }
    return MP_OBJ_FROM_PTR(self);
}

static void mp_machine_pwm_deinit(machine_pwm_obj_t *self) {
    // Only this channel's output stops; pwm_stop() halts the whole slice
    // (all 4 channels), which would be a surprising side effect on any
    // sibling channel still in use, so just zero this channel's duty.
    pwm_slice_channel_active[self->slice][self->channel] = false;
    pwm_slice_channel_duty_u16[self->slice][self->channel] = 0;
    if (pwm_slice_freq[self->slice] != 0) {
        pwm_set_percent(self->slice, self->channel, 0);
    }
}
