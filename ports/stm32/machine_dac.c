// This file is part of the MicroPython project, http://micropython.org/
// SPDX-License-Identifier: MIT

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"

#if defined(STM32C5) && MICROPY_PY_MACHINE_DAC

#include "pin.h"
#include "analog_c5.h"
#include "stm32c5xx_ll_bus.h"
#include "stm32c5xx_ll_dac.h"

extern const mp_obj_type_t machine_dac_type;

// DAC1 has one external channel, on PA4. All constructors refer to this
// peripheral: no heap object or independent ownership for aliases is needed.
static const mp_obj_base_t machine_dac_obj = { &machine_dac_type };
static bool machine_dac_active;

void dac_deinit_all(void) {
    if (!machine_dac_active) {
        return;
    }
    // Never touch peripheral registers through a disabled bus clock. The gate
    // is DAC-only; do not reset or disable the shared ADC/DAC kernel clock.
    if (LL_AHB2_GRP1_IsEnabledClock(LL_AHB2_GRP1_PERIPH_DAC1)) {
        DAC1->CR &= ~DAC_CR_EN1;
        DAC1->CR = 0;
        DAC1->MCR = LL_DAC_OUTPUT_BUFFER_DISABLE;
        LL_AHB2_GRP1_DisableClock(LL_AHB2_GRP1_PERIPH_DAC1);
    }
    machine_dac_active = false;
}

static mp_obj_t machine_dac_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type;
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    if (mp_obj_is_int(args[0])) {
        if (mp_obj_get_int(args[0]) != 1) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid DAC ID"));
        }
    } else if (pin_find(args[0]) != pin_A4) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid DAC pin"));
    }

    // The 18-MHz analog kernel must remain independent of machine.freq().
    // The shared helper refuses mux changes when another analog user is live.
    int ret = analog_c5_init_clock();
    if (ret != 0) {
        mp_raise_OSError(-ret);
    }
    if (!machine_dac_active || !LL_AHB2_GRP1_IsEnabledClock(LL_AHB2_GRP1_PERIPH_DAC1)) {
        LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_DAC1);
        mp_hal_pin_config(pin_A4, MP_HAL_PIN_MODE_ANALOG, MP_HAL_PIN_PULL_NONE, 0);
        // EN must be cleared in a separate write before changing CEN or MODE.
        DAC1->CR &= ~DAC_CR_EN1;
        DAC1->CR = 0;
        uint32_t hclk = HAL_RCC_GetHCLKFreq();
        uint32_t hfsel = hclk > 160000000 ? LL_DAC_HIGH_FREQ_MODE_ABOVE_160MHZ
            : hclk > 80000000 ? LL_DAC_HIGH_FREQ_MODE_ABOVE_80MHZ
            : LL_DAC_HIGH_FREQ_MODE_DISABLED;
        // Normal mode, external output with buffer, unsigned 12-bit data;
        // triggering, sample-and-hold, DMA and waveform generation all off.
        DAC1->MCR = hfsel | LL_DAC_OUTPUT_BUFFER_ENABLE;
        DAC1->DHR12R1 = 0;
        DAC1->CR = DAC_CR_EN1;
        // HAL2 StartChannel uses 15 us (more conservative than LL's 8 us).
        mp_hal_delay_us(15);
        machine_dac_active = true;
    }
    return MP_OBJ_FROM_PTR(&machine_dac_obj);
}

static mp_obj_t machine_dac_deinit(mp_obj_t self_in) {
    (void)self_in;
    dac_deinit_all();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_dac_deinit_obj, machine_dac_deinit);

static mp_obj_t machine_dac_write(mp_obj_t self_in, mp_obj_t value_in) {
    (void)self_in;
    mp_int_t value = mp_obj_get_int(value_in);
    if (value < 0 || value > 4095) {
        mp_raise_ValueError(MP_ERROR_TEXT("value out of range"));
    }
    if (!machine_dac_active || !LL_AHB2_GRP1_IsEnabledClock(LL_AHB2_GRP1_PERIPH_DAC1)) {
        mp_raise_OSError(MP_ENODEV);
    }
    // TEN1=0 transfers DHR to DOR automatically, without a software trigger.
    DAC1->DHR12R1 = value;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_dac_write_obj, machine_dac_write);

static const mp_rom_map_elem_t machine_dac_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_dac_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&machine_dac_write_obj) },
};
static MP_DEFINE_CONST_DICT(machine_dac_locals_dict, machine_dac_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_dac_type,
    MP_QSTR_DAC,
    MP_TYPE_FLAG_NONE,
    make_new, machine_dac_make_new,
    locals_dict, &machine_dac_locals_dict
    );

#endif // STM32C5 && MICROPY_PY_MACHINE_DAC
