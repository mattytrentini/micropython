/* Shared STM32C5 ADC/DAC kernel clock policy. */
#ifndef MICROPY_INCLUDED_STM32_ANALOG_C5_H
#define MICROPY_INCLUDED_STM32_ANALOG_C5_H

#include <stdbool.h>
#include "py/mperrno.h"
#include "stm32c5xx_ll_rcc.h"
#include "irq.h"

static inline bool analog_c5_is_active(void) {
    if ((RCC->AHB2ENR & RCC_AHB2ENR_ADC12EN) && ((ADC1->CR | ADC2->CR) & ADC_CR_ADEN)) {
        return true;
    }
    #if defined(ADC3)
    if ((RCC->AHB2ENR & RCC_AHB2ENR_ADC3EN) && (ADC3->CR & ADC_CR_ADEN)) {
        return true;
    }
    #endif
    return (RCC->AHB2ENR & RCC_AHB2ENR_DAC1EN) && (DAC1->CR & DAC_CR_EN1);
}

static inline int analog_c5_init_clock(void) {
    const uint32_t mask = RCC_CCIPR2_ADCDACSEL | RCC_CCIPR2_ADCDACPRE;
    // C5 startup keeps PSIS at 144 MHz independently of the system clock.
    // Divide it to 18 MHz for both converters; do not disturb the shared mux.
    const uint32_t config = LL_RCC_ADCDAC_CLKSOURCE_PSIS | LL_RCC_ADCDAC_PRESCALER_8;
    if (!LL_RCC_PSIS_IsReady()) {
        return -MP_EIO;
    }
    uint32_t irq = disable_irq();
    int ret = 0;
    if ((RCC->CCIPR2 & mask) != config) {
        // The mux/divider are shared, including across the separate ADC3 bus gate.
        if (analog_c5_is_active()) {
            ret = -MP_EBUSY;
        } else {
            STM32_MODIFY_REG(RCC->CCIPR2, mask, config);
        }
    }
    enable_irq(irq);
    return ret;
}

#endif // MICROPY_INCLUDED_STM32_ANALOG_C5_H
