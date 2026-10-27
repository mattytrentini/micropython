// Native STM32C5 timer backend.
// SPDX-License-Identifier: MIT

#ifndef MICROPY_INCLUDED_STM32_TIMER_C5_H
#define MICROPY_INCLUDED_STM32_TIMER_C5_H

#include "stm32c5xx_ll_bus.h"
#include "stm32c5xx_ll_tim.h"

static void timer_c5_clock_enable(uint id, bool reset) {
    uint32_t clock;
    bool apb2 = false;
    switch (id) {
        case 2:
            clock = LL_APB1_GRP1_PERIPH_TIM2;
            break;
        case 3:
            clock = LL_APB1_GRP1_PERIPH_TIM3;
            break;
        case 4:
            clock = LL_APB1_GRP1_PERIPH_TIM4;
            break;
        case 5:
            clock = LL_APB1_GRP1_PERIPH_TIM5;
            break;
        case 6:
            clock = LL_APB1_GRP1_PERIPH_TIM6;
            break;
        case 7:
            clock = LL_APB1_GRP1_PERIPH_TIM7;
            break;
        case 12:
            clock = LL_APB1_GRP1_PERIPH_TIM12;
            break;
        case 1:
            clock = LL_APB2_GRP1_PERIPH_TIM1;
            apb2 = true;
            break;
        case 8:
            clock = LL_APB2_GRP1_PERIPH_TIM8;
            apb2 = true;
            break;
        case 15:
            clock = LL_APB2_GRP1_PERIPH_TIM15;
            apb2 = true;
            break;
        case 16:
            clock = LL_APB2_GRP1_PERIPH_TIM16;
            apb2 = true;
            break;
        case 17:
            clock = LL_APB2_GRP1_PERIPH_TIM17;
            apb2 = true;
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("invalid timer"));
    }
    if (apb2) {
        LL_APB2_GRP1_EnableClock(clock);
        LL_APB2_GRP1_EnableClockLowPower(clock);
        if (reset) {
            LL_APB2_GRP1_ForceReset(clock);
            LL_APB2_GRP1_ReleaseReset(clock);
        }
    } else {
        LL_APB1_GRP1_EnableClock(clock);
        LL_APB1_GRP1_EnableClockLowPower(clock);
        if (reset) {
            LL_APB1_GRP1_ForceReset(clock);
            LL_APB1_GRP1_ReleaseReset(clock);
        }
    }
}

static void timer_c5_base_init(timer_handle_t *handle) {
    TIM_TypeDef *tim = handle->Instance;
    LL_TIM_DisableCounter(tim);
    LL_TIM_DisableIT_UPDATE(tim);
    if (IS_TIM_COUNTER_MODE_SELECT_INSTANCE(tim)) {
        LL_TIM_SetCounterMode(tim, handle->Init.CounterMode);
    }
    if (IS_TIM_CLOCK_DIVISION_INSTANCE(tim)) {
        LL_TIM_SetClockDivision(tim, handle->Init.ClockDivision);
    }
    if (IS_TIM_SLAVE_INSTANCE(tim)) {
        tim->SMCR &= ~(TIM_SMCR_SMS | TIM_SMCR_ECE);
    }
    LL_TIM_SetPrescaler(tim, handle->Init.Prescaler);
    LL_TIM_SetAutoReload(tim, handle->Init.Period);
    if (IS_TIM_REPETITION_COUNTER_INSTANCE(tim)) {
        LL_TIM_SetRepetitionCounter(tim, handle->Init.RepetitionCounter);
    }
    LL_TIM_GenerateEvent_UPDATE(tim);
    LL_TIM_ClearFlag_UPDATE(tim);
    handle->State = true;
}

static bool timer_c5_has_channel(TIM_TypeDef *tim, uint channel) {
    switch (channel) {
        case 1:
            return IS_TIM_CC1_INSTANCE(tim);
        case 2:
            return IS_TIM_CC2_INSTANCE(tim);
        case 3:
            return IS_TIM_CC3_INSTANCE(tim);
        default:
            return IS_TIM_CC4_INSTANCE(tim);
    }
}

static bool timer_c5_has_complementary(TIM_TypeDef *tim, uint channel) {
    switch (channel) {
        case 1:
            return IS_TIM_CC1N_INSTANCE(tim);
        case 2:
            return IS_TIM_CC2N_INSTANCE(tim);
        case 3:
            return IS_TIM_CC3N_INSTANCE(tim);
        default:
            return IS_TIM_CC4N_INSTANCE(tim);
    }
}

static volatile uint32_t *timer_c5_ccmr(TIM_TypeDef *tim, uint channel) {
    return channel <= 2 ? &tim->CCMR1 : &tim->CCMR2;
}

static void timer_c5_configure_output(timer_handle_t *handle, uint channel,
    uint32_t mode, uint32_t pulse, uint32_t polarity, bool pwm) {
    TIM_TypeDef *tim = handle->Instance;
    uint32_t ll_channel = LL_TIM_CHANNEL_CH1 << ((channel - 1) * 4);
    bool complementary = timer_c5_has_complementary(tim, channel);
    uint32_t ll_channel_n = complementary ? ll_channel << 2 : 0;
    LL_TIM_CC_DisableChannel(tim, ll_channel | ll_channel_n);
    // Clear input filters/prescalers and previous output-mode extension bits.
    uint32_t shift = ((channel - 1) & 1) * 8;
    *timer_c5_ccmr(tim, channel) &= ~((0xffu | TIM_CCMR1_OC1M) << shift);
    LL_TIM_OC_SetMode(tim, ll_channel, mode);
    *timer_compare_register(handle, (channel - 1) * 4) = pulse;
    LL_TIM_OC_SetPolarity(tim, ll_channel, polarity);
    if (IS_TIM_BREAK_INSTANCE(tim)) {
        LL_TIM_OC_SetIdleState(tim, ll_channel, LL_TIM_OCIDLESTATE_SET);
    }
    if (complementary) {
        LL_TIM_OC_SetPolarity(tim, ll_channel_n, polarity);
        LL_TIM_OC_SetIdleState(tim, ll_channel_n, LL_TIM_OCIDLESTATE_SET);
    }
    if (pwm) {
        LL_TIM_OC_EnablePreload(tim, ll_channel);
    }
}

static void timer_c5_configure_input(TIM_TypeDef *tim, uint channel, uint32_t polarity) {
    uint32_t ll_channel = LL_TIM_CHANNEL_CH1 << ((channel - 1) * 4);
    LL_TIM_CC_DisableChannel(tim, ll_channel);
    uint32_t shift = ((channel - 1) & 1) * 8;
    volatile uint32_t *ccmr = timer_c5_ccmr(tim, channel);
    *ccmr = (*ccmr & ~((0xffu | TIM_CCMR1_OC1M) << shift))
        | (TIM_CCMR1_CC1S_0 << shift);
    LL_TIM_IC_SetPolarity(tim, ll_channel, polarity);
    tim->SR = ~((TIM_SR_CC1IF | TIM_SR_CC1OF) << (channel - 1));
}

static void timer_c5_start_channel(TIM_TypeDef *tim, uint channel, bool irq, bool output) {
    LL_TIM_CC_EnableChannel(tim, LL_TIM_CHANNEL_CH1 << ((channel - 1) * 4));
    if (output && IS_TIM_BREAK_INSTANCE(tim)) {
        LL_TIM_EnableAllOutputs(tim);
    }
    if (irq) {
        tim->DIER |= 1u << channel;
    }
    LL_TIM_EnableCounter(tim);
}

static void timer_c5_start_complementary(TIM_TypeDef *tim, uint channel) {
    if (timer_c5_has_complementary(tim, channel)) {
        LL_TIM_CC_EnableChannel(tim, LL_TIM_CHANNEL_CH1N << ((channel - 1) * 4));
        LL_TIM_EnableAllOutputs(tim);
    }
}

static const pin_af_obj_t *timer_c5_find_af(const machine_pin_obj_t *pin,
    uint timer, uint channel, bool output) {
    for (uint i = 0; i < pin->num_af; ++i) {
        const pin_af_obj_t *af = &pin->af[i];
        if (af->fn == AF_FN_TIM && af->unit == timer
            && (af->type == AF_PIN_TYPE_TIM_CH1 + channel - 1
                || (channel == 1 && af->type == AF_PIN_TYPE_TIM_CH1_ETR)
                || (output && channel <= 3 && af->type == AF_PIN_TYPE_TIM_CH1N + channel - 1)
                || (output && channel == 4 && af->type == AF_PIN_TYPE_TIM_CH4N))) {
            return af;
        }
    }
    return NULL;
}

#endif // MICROPY_INCLUDED_STM32_TIMER_C5_H
