/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 MicroPython contributors
 */

// Included by extmod/machine_adc.c, not compiled separately.
#include "py/mphal.h"
#include "py/mperrno.h"
#include "pin.h"
#include "irq.h"
#include "analog_c5.h"
#include "stm32c5xx_ll_adc.h"

#define MACHINE_ADC_C5_CORE_VREF (256)
#define MACHINE_ADC_C5_CORE_TEMP (257)
#define MACHINE_ADC_C5_VREF (65535)
#define MACHINE_ADC_C5_TIMEOUT_MS (10)

#define MICROPY_PY_MACHINE_ADC_CLASS_CONSTANTS \
    { MP_ROM_QSTR(MP_QSTR_VREF), MP_ROM_INT(MACHINE_ADC_C5_VREF) }, \
    { MP_ROM_QSTR(MP_QSTR_CORE_VREF), MP_ROM_INT(MACHINE_ADC_C5_CORE_VREF) }, \
    { MP_ROM_QSTR(MP_QSTR_CORE_TEMP), MP_ROM_INT(MACHINE_ADC_C5_CORE_TEMP) },

typedef struct _machine_adc_obj_t {
    mp_obj_base_t base;
    ADC_TypeDef *adc;
    uint16_t channel;
    bool active;
} machine_adc_obj_t;

// The CPU cycle counter continues running with interrupts masked, unlike the
// millisecond tick. At 18MHz ADC clock even calibration takes less than 2ms
// (HAL specifies at most 31849 ADC cycles); allow 10ms for each operation.
static int machine_adc_c5_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value) {
    uint32_t start = mp_hal_ticks_cpu();
    uint32_t timeout = (SystemCoreClock / 1000) * MACHINE_ADC_C5_TIMEOUT_MS;
    while ((*reg & mask) != value) {
        if ((uint32_t)(mp_hal_ticks_cpu() - start) >= timeout) {
            return -MP_ETIMEDOUT;
        }
    }
    return 0;
}

static int machine_adc_c5_start(ADC_TypeDef *adc) {
    int ret = analog_c5_init_clock();
    if (ret != 0) {
        return ret;
    }

    if (adc == ADC1 || adc == ADC2) {
        RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;
        (void)RCC->AHB2ENR;
        // Internal paths belong to the common block and may only be changed
        // with BOTH ADCs disabled. Enable both up front, including when ADC2
        // is the first consumer. Never reset ADC12 to initialise a sibling.
        uint32_t paths = ADCC_CCR_VREFEN | ADCC_CCR_TSEN;
        if ((ADC12_COMMON->CCR & paths) != paths) {
            if ((ADC1->CR | ADC2->CR) & ADC_CR_ADEN) {
                return -MP_EBUSY;
            }
            ADC12_COMMON->CCR |= paths;
            mp_hal_delay_us(LL_ADC_DELAY_TEMPSENSOR_STAB_US);
        }
    #if defined(ADC3)
    } else {
        RCC->AHB2ENR |= RCC_AHB2ENR_ADC3EN;
        (void)RCC->AHB2ENR;
    #endif
    }

    if (adc->CR & (ADC_CR_ADCAL | ADC_CR_ADDIS | ADC_CR_ADSTART | ADC_CR_JADSTART
                   | ADC_CR_ADSTP | ADC_CR_JADSTP)) {
        return -MP_EBUSY;
    }
    if (adc->CR & ADC_CR_ADEN) {
        return 0;
    }

    LL_ADC_DisableDeepPowerDown(adc);
    LL_ADC_EnableInternalRegulator(adc);
    mp_hal_delay_us(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);
    ret = machine_adc_c5_wait(&adc->ISR, ADC_ISR_LDORDY, ADC_ISR_LDORDY);
    if (ret != 0) {
        return ret;
    }

    // Single-ended, 12-bit, right-aligned, software-triggered single conversion,
    // no DMA, oversampling, discontinuous mode, or injected auto conversions.
    adc->IER = 0;
    adc->CFGR1 = 0;
    adc->CFGR2 = 0;
    LL_ADC_StartCalibration(adc, LL_ADC_IN_SINGLE_ENDED);
    ret = machine_adc_c5_wait(&adc->CR, ADC_CR_ADCAL, 0);
    if (ret != 0) {
        return ret;
    }
    // LL requires four ADC cycles between calibration and enable: 1us is
    // eighteen cycles with the shared PSIS / 8 kernel clock.
    mp_hal_delay_us(1);
    LL_ADC_ClearFlag_ADRDY(adc);
    LL_ADC_Enable(adc);
    ret = machine_adc_c5_wait(&adc->ISR, ADC_ISR_ADRDY, ADC_ISR_ADRDY);
    if (ret == 0) {
        mp_hal_delay_us(1); // tSTAB: one conversion cycle, rounded up
    }
    return ret;
}

void machine_adc_deinit_all(void) {
    // Only used at soft reset, when all logical ADC owners are discarded.
    // ADC12 has a shared reset. Neither these reset lines nor bus gates touch
    // DAC1 or the common ADC/DAC kernel clock mux.
    RCC->AHB2RSTR |= RCC_AHB2RSTR_ADC12RST;
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_ADC12RST;
    RCC->AHB2ENR &= ~RCC_AHB2ENR_ADC12EN;
    #if defined(ADC3)
    RCC->AHB2RSTR |= RCC_AHB2RSTR_ADC3RST;
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_ADC3RST;
    RCC->AHB2ENR &= ~RCC_AHB2ENR_ADC3EN;
    #endif
}

static void mp_machine_adc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)kind;
    unsigned id = self->adc == ADC2 ? 2 : 1;
    #if defined(ADC3)
    if (self->adc == ADC3) {
        id = 3;
    }
    #endif
    mp_printf(print, "<ADC%u channel=%u%s>", id, self->channel, self->active ? "" : " deinitialized");
}

static mp_obj_t mp_machine_adc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    ADC_TypeDef *adc = ADC1;
    const machine_pin_obj_t *pin = NULL;
    mp_int_t channel;
    if (mp_obj_is_int(args[0])) {
        channel = mp_obj_get_int(args[0]);
        // Numeric channels select ADC1, as on other STM32 parts. ADC1_IN12
        // and IN13 are internal-only on C5; accept their hardware numbers too.
        if (channel == 12) {
            channel = MACHINE_ADC_C5_CORE_TEMP;
        } else if (channel == 13) {
            channel = MACHINE_ADC_C5_CORE_VREF;
        } else if (!((channel >= 0 && channel <= 11)
                     || channel == MACHINE_ADC_C5_CORE_VREF || channel == MACHINE_ADC_C5_CORE_TEMP
                     || channel == MACHINE_ADC_C5_VREF)) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid ADC channel"));
        }
    } else {
        pin = pin_find(args[0]);
        if (pin->adc_num & PIN_ADC1) {
            adc = ADC1;
        } else if (pin->adc_num & PIN_ADC2) {
            adc = ADC2;
        #if defined(ADC3)
        } else if (pin->adc_num & PIN_ADC3) {
            adc = ADC3;
        #endif
        } else {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Pin(%q) does not have ADC capabilities"), pin->name);
        }
        channel = pin->adc_channel;
        // ADC1's last two inputs are internal-only. For the other instances,
        // external input availability comes from the generated pin metadata.
        if (channel > 13 || (adc == ADC1 && channel > 11)) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid ADC pin channel"));
        }
    }

    machine_adc_obj_t *self = mp_obj_malloc(machine_adc_obj_t, type);
    self->adc = adc;
    self->channel = channel;
    self->active = false;
    if (channel != MACHINE_ADC_C5_VREF) {
        int ret = machine_adc_c5_start(adc);
        if (ret != 0) {
            mp_raise_OSError(-ret);
        }
        if (pin != NULL) {
            mp_hal_pin_config(pin, MP_HAL_PIN_MODE_ADC, MP_HAL_PIN_PULL_NONE, 0);
        }
    }
    self->active = true;
    return MP_OBJ_FROM_PTR(self);
}

static void mp_machine_adc_deinit(machine_adc_obj_t *self) {
    if (!self->active) {
        return;
    }
    if (self->channel == MACHINE_ADC_C5_VREF) {
        self->active = false;
        return;
    }
    // Disable this instance only, never the shared reset or bus gate. Other
    // logical handles re-enable their instance before their next conversion.
    ADC_TypeDef *adc = self->adc;
    int ret = 0;
    if (adc->CR & ADC_CR_ADSTART) {
        LL_ADC_REG_StopConversion(adc);
        ret = machine_adc_c5_wait(&adc->CR, ADC_CR_ADSTART | ADC_CR_ADSTP, 0);
    }
    if (ret == 0 && (adc->CR & ADC_CR_ADEN)) {
        LL_ADC_Disable(adc);
        ret = machine_adc_c5_wait(&adc->CR, ADC_CR_ADEN | ADC_CR_ADDIS, 0);
    }
    if (ret != 0) {
        mp_raise_OSError(-ret);
    }
    self->active = false;
}

static mp_int_t mp_machine_adc_read_u16(machine_adc_obj_t *self) {
    if (!self->active) {
        mp_raise_OSError(MP_ENODEV);
    }
    if (self->channel == MACHINE_ADC_C5_VREF) {
        return 65535; // STM32's explicitly synthetic full-scale channel
    }

    ADC_TypeDef *adc = self->adc;
    if (!(adc->CR & ADC_CR_ADEN)) {
        int ret = machine_adc_c5_start(adc);
        if (ret != 0) {
            mp_raise_OSError(-ret);
        }
    }
    uint32_t channel = self->channel;
    uint32_t sampling = LL_ADC_SAMPLINGTIME_25CYCLES;
    if (channel == MACHINE_ADC_C5_CORE_TEMP) {
        channel = 12;
        sampling = LL_ADC_SAMPLINGTIME_289CYCLES;
    } else if (channel == MACHINE_ADC_C5_CORE_VREF) {
        channel = 13;
        sampling = LL_ADC_SAMPLINGTIME_289CYCLES;
    }

    // Protect selection through DR read against hard-IRQ ADC users. The DWT
    // deadline above bounds even a failed conversion with interrupts disabled.
    uint32_t irq = disable_irq();
    int ret;
    uint32_t raw = 0;
    if (!(adc->CR & ADC_CR_ADEN) || (adc->CR & (ADC_CR_ADSTART | ADC_CR_JADSTART | ADC_CR_ADCAL))) {
        ret = -MP_EBUSY;
    } else {
        adc->PCSEL = 1U << channel;
        adc->SQR1 = channel << ADC_SQR1_SQ1_Pos;
        volatile uint32_t *smpr = channel < 10 ? &adc->SMPR1 : &adc->SMPR2;
        uint32_t shift = (channel % 10) * 3;
        *smpr = (*smpr & ~(7U << shift)) | (sampling << shift);
        adc->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
        LL_ADC_REG_StartConversion(adc);
        ret = machine_adc_c5_wait(&adc->ISR, ADC_ISR_EOC, ADC_ISR_EOC);
        if (ret == 0) {
            raw = adc->DR;
        }
    }
    enable_irq(irq);
    if (ret != 0) {
        mp_raise_OSError(-ret);
    }
    return (raw << 4) | (raw >> 8);
}
