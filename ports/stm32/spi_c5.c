// Native STM32C5 master SPI using the shared machine.SPI frontend.
// SPDX-License-Identifier: MIT
#include <string.h>
#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "extmod/modmachine.h"
#include "spi.h"
#include "irq.h"

#define SPI_C5_ERRORS (SPI_SR_OVR | SPI_SR_MODF | SPI_SR_UDR | SPI_SR_TIFRE | SPI_SR_CRCE)
#define SPI_C5_CLEAR (SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_UDRC | SPI_IFCR_OVRC | SPI_IFCR_CRCEC | SPI_IFCR_TIFREC | SPI_IFCR_MODFC | SPI_IFCR_SUSPC)

#if defined(MICROPY_HW_SPI1_SCK)
static spi_state_t spi1 = {.instance = SPI1, .bits = 8};
#endif
#if defined(MICROPY_HW_SPI2_SCK)
static spi_state_t spi2 = {.instance = SPI2, .bits = 8};
#endif
#if defined(MICROPY_HW_SPI3_SCK)
static spi_state_t spi3 = {.instance = SPI3, .bits = 8};
#endif
const spi_t spi_obj[6] = {
    #if defined(MICROPY_HW_SPI1_SCK)
    {&spi1},
    #else
    {NULL},
    #endif
    #if defined(MICROPY_HW_SPI2_SCK)
    {&spi2},
    #else
    {NULL},
    #endif
    #if defined(MICROPY_HW_SPI3_SCK)
    {&spi3},
    #else
    {NULL},
    #endif
};

static void spi_c5_clock(const spi_t *self, bool enable) {
    uint32_t mask;
    if (self->spi->instance == SPI1) {
        mask = LL_APB2_GRP1_PERIPH_SPI1;
        if (enable) {
            LL_APB2_GRP1_EnableClock(mask);
            LL_APB2_GRP1_EnableClockLowPower(mask);
        } else {
            LL_APB2_GRP1_DisableClockLowPower(mask);
            LL_APB2_GRP1_DisableClock(mask);
        }
    } else {
        mask = self->spi->instance == SPI2 ? LL_APB1_GRP1_PERIPH_SPI2 : LL_APB1_GRP1_PERIPH_SPI3;
        if (enable) {
            LL_APB1_GRP1_EnableClock(mask);
            LL_APB1_GRP1_EnableClockLowPower(mask);
        } else {
            LL_APB1_GRP1_DisableClockLowPower(mask);
            LL_APB1_GRP1_DisableClock(mask);
        }
    }
}

static void spi_c5_reset(const spi_t *self) {
    if (self->spi->instance == SPI1) {
        LL_APB2_GRP1_ForceReset(LL_APB2_GRP1_PERIPH_SPI1);
        LL_APB2_GRP1_ReleaseReset(LL_APB2_GRP1_PERIPH_SPI1);
    } else {
        uint32_t mask = self->spi->instance == SPI2 ? LL_APB1_GRP1_PERIPH_SPI2 : LL_APB1_GRP1_PERIPH_SPI3;
        LL_APB1_GRP1_ForceReset(mask);
        LL_APB1_GRP1_ReleaseReset(mask);
    }
}

static void spi_c5_configure(const spi_t *self) {
    spi_state_t *s = self->spi;
    SPI_TypeDef *hw = s->instance;
    hw->CR1 = SPI_CR1_SSI;
    hw->CR2 = 0;
    hw->CFG1 = (s->bits - 1) | ((uint32_t)s->divider << SPI_CFG1_MBR_Pos);
    hw->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_AFCNTR
        | (s->polarity ? SPI_CFG2_CPOL : 0) | (s->phase ? SPI_CFG2_CPHA : 0)
        | (s->lsb ? SPI_CFG2_LSBFRST : 0);
    hw->IER = 0;
    hw->IFCR = SPI_C5_CLEAR;
}

void spi_init0(void) {
    LL_RCC_SetSPIClockSource(LL_RCC_SPI1_CLKSOURCE_PCLK2);
    LL_RCC_SetSPIClockSource(LL_RCC_SPI2_CLKSOURCE_PCLK1);
    LL_RCC_SetSPIClockSource(LL_RCC_SPI3_CLKSOURCE_PCLK1);
}

int spi_find_index(mp_obj_t id) {
    int index = -1;
    if (mp_obj_is_str(id)) {
        const char *name = mp_obj_str_get_str(id);
        #if defined(MICROPY_HW_SPI1_NAME)
        if (strcmp(name, MICROPY_HW_SPI1_NAME) == 0) { index = 1; }
        #endif
        #if defined(MICROPY_HW_SPI2_NAME)
        if (strcmp(name, MICROPY_HW_SPI2_NAME) == 0) { index = 2; }
        #endif
        #if defined(MICROPY_HW_SPI3_NAME)
        if (strcmp(name, MICROPY_HW_SPI3_NAME) == 0) { index = 3; }
        #endif
        (void)name;
    } else {
        index = mp_obj_get_int(id);
    }
    if (index < 1 || index > 3 || spi_obj[index - 1].spi == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("SPI bus doesn't exist"));
    }
    if (MICROPY_HW_SPI_IS_RESERVED(index)) {
        mp_raise_ValueError(MP_ERROR_TEXT("SPI bus is reserved"));
    }
    return index;
}

void spi_set_params(const spi_t *self, uint32_t prescale, int32_t baudrate,
    int32_t polarity, int32_t phase, int32_t bits, int32_t firstbit) {
    if ((baudrate != -1 && baudrate <= 0)
        || (polarity != -1 && polarity != 0 && polarity != 1)
        || (phase != -1 && phase != 0 && phase != 1)
        || (bits != -1 && bits != 8 && bits != 16)
        || (firstbit != -1 && firstbit != SPI_FIRSTBIT_MSB && firstbit != SPI_FIRSTBIT_LSB)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid SPI configuration"));
    }
    spi_state_t *s = self->spi;
    uint32_t irq = disable_irq();
    if (s->busy) {
        enable_irq(irq);
        mp_raise_OSError(MP_EBUSY);
    }
    if (prescale != 0xffffffff || baudrate != -1) {
        uint32_t clock = HAL_RCC_SPI_GetKernelClkFreq(s->instance);
        uint32_t div = prescale != 0xffffffff ? prescale : (clock + baudrate - 1) / baudrate;
        s->divider = 0;
        while (s->divider < 7 && (2u << s->divider) < div) { ++s->divider; }
        s->baudrate = clock >> (s->divider + 1);
    }
    if (polarity != -1) { s->polarity = polarity; }
    if (phase != -1) { s->phase = phase; }
    if (bits != -1) { s->bits = bits; }
    if (firstbit != -1) { s->lsb = firstbit == SPI_FIRSTBIT_LSB; }
    if (s->initialized) { spi_c5_configure(self); }
    enable_irq(irq);
}

int spi_init(const spi_t *self, bool enable_nss_pin) {
    if (enable_nss_pin) { return -MP_EOPNOTSUPP; }
    if (self->spi->busy) { return -MP_EBUSY; }
    const machine_pin_obj_t *pins[3] = {NULL, NULL, NULL};
    unsigned id = self - spi_obj + 1;
    switch (id) {
        #if defined(MICROPY_HW_SPI1_SCK)
        case 1: pins[0] = MICROPY_HW_SPI1_SCK; pins[1] = MICROPY_HW_SPI1_MOSI; pins[2] = MICROPY_HW_SPI1_MISO; break;
        #endif
        #if defined(MICROPY_HW_SPI2_SCK)
        case 2: pins[0] = MICROPY_HW_SPI2_SCK; pins[1] = MICROPY_HW_SPI2_MOSI; pins[2] = MICROPY_HW_SPI2_MISO; break;
        #endif
        #if defined(MICROPY_HW_SPI3_SCK)
        case 3: pins[0] = MICROPY_HW_SPI3_SCK; pins[1] = MICROPY_HW_SPI3_MOSI; pins[2] = MICROPY_HW_SPI3_MISO; break;
        #endif
        default: return -MP_EINVAL;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (pins[i] != NULL && !mp_hal_pin_config_alt(pins[i], MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, AF_FN_SPI, id)) {
            return -MP_EINVAL;
        }
    }
    spi_c5_clock(self, true);
    spi_c5_reset(self);
    spi_c5_configure(self);
    self->spi->initialized = true;
    return 0;
}

// Never return to Python with an unquiesced master holding raw buffer pointers.
static void spi_c5_stop(const spi_t *self, bool reset) {
    spi_state_t *s = self->spi;
    s->instance->CFG1 &= ~(SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
    s->instance->CR1 &= ~SPI_CR1_SPE;
    if (reset) { spi_c5_reset(self); }
    if (reset) { spi_c5_configure(self); }
    s->instance->IFCR = SPI_C5_CLEAR;
}

void spi_deinit(const spi_t *self) {
    if (self->spi->busy) { mp_raise_OSError(MP_EBUSY); }
    if (self->spi->initialized) {
        spi_c5_stop(self, true);
        spi_c5_clock(self, false);
        self->spi->initialized = false;
    }
}

void spi_deinit_all(void) {
    for (unsigned i = 0; i < 3; ++i) {
        if (spi_obj[i].spi != NULL) { spi_deinit(&spi_obj[i]); }
    }
}

typedef struct _spi_c5_deadline_t {
    uint64_t remaining;
    uint32_t previous;
    uint32_t started_ms;
    uint32_t timeout_ms;
    bool wall_clock;
} spi_c5_deadline_t;

static int spi_c5_check(SPI_TypeDef *hw, spi_c5_deadline_t *deadline, bool poll_vm) {
    if (hw->SR & SPI_C5_ERRORS) { return -MP_EIO; }
    if (deadline->wall_clock) {
        if ((uint32_t)(mp_hal_ticks_ms() - deadline->started_ms) >= deadline->timeout_ms) {
            return -MP_ETIMEDOUT;
        }
    } else {
        // SysTick cannot advance in a masked or higher-priority IRQ context.
        uint32_t now = mp_hal_ticks_cpu();
        uint32_t elapsed = now - deadline->previous;
        deadline->previous = now;
        if (elapsed >= deadline->remaining) { return -MP_ETIMEDOUT; }
        deadline->remaining -= elapsed;
    }
    if (poll_vm) { mp_event_wait_ms(1); }
    return 0;
}

static int spi_c5_chunk(const spi_t *self, size_t len, const uint8_t *src, uint8_t *dest,
    bool poll_vm, spi_c5_deadline_t *deadline) {
    spi_state_t *s = self->spi;
    SPI_TypeDef *hw = s->instance;
    unsigned width = s->bits / 8;
    hw->IFCR = SPI_C5_CLEAR;
    hw->CR2 = len / width;
    hw->CR1 |= SPI_CR1_SPE;
    hw->CR1 |= SPI_CR1_CSTART;
    for (size_t i = 0; i < len; i += width) {
        while (!(hw->SR & SPI_SR_TXP)) {
            int ret = spi_c5_check(hw, deadline, false);
            if (ret != 0) { return ret; }
        }
        uint16_t value = 0xffff;
        if (src) {
            if (width == 1) { value = src[i]; }
            else { memcpy(&value, src + i, 2); }
        }
        if (width == 1) { LL_SPI_TransmitData8(hw, value); }
        else { LL_SPI_TransmitData16(hw, value); }
        while (!(hw->SR & SPI_SR_RXP)) {
            int ret = spi_c5_check(hw, deadline, false);
            if (ret != 0) { return ret; }
        }
        value = width == 1 ? LL_SPI_ReceiveData8(hw) : LL_SPI_ReceiveData16(hw);
        if (dest) {
            if (width == 1) { dest[i] = value; }
            else { memcpy(dest + i, &value, 2); }
        }
        if ((i & 127) == 0) {
            int ret = spi_c5_check(hw, deadline, false);
            if (ret != 0) { return ret; }
            if (poll_vm) { mp_event_handle_nowait(); }
        }
    }
    while ((hw->SR & (SPI_SR_EOT | SPI_SR_TXC)) != (SPI_SR_EOT | SPI_SR_TXC)) {
        int ret = spi_c5_check(hw, deadline, false);
        if (ret != 0) { return ret; }
    }
    if (hw->SR & SPI_C5_ERRORS) { return -MP_EIO; }
    hw->CFG1 &= ~(SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
    hw->CR1 &= ~SPI_CR1_SPE;
    return 0;
}

void spi_transfer(const spi_t *self, size_t len, const uint8_t *src, uint8_t *dest, uint32_t timeout) {
    spi_state_t *s = self->spi;
    if (!s->initialized) { mp_raise_OSError(MP_ENODEV); }
    if (s->bits == 16 && (len & 1)) { mp_raise_ValueError(MP_ERROR_TEXT("16-bit SPI requires an even byte count")); }
    if (s->busy) { mp_raise_OSError(MP_EBUSY); }
    if (len == 0) { return; }
    s->busy = true;
    bool poll_vm = query_irq() == 0 && __get_IPSR() == 0;
    spi_c5_deadline_t deadline = {
        (uint64_t)timeout * (SystemCoreClock / 1000), mp_hal_ticks_cpu(),
        mp_hal_ticks_ms(), timeout, poll_vm
    };
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        int ret = 0;
        while (ret == 0 && len) {
            size_t chunk = len < 65535 ? len : 65535;
            if (s->bits == 16) { chunk &= ~(size_t)1; }
            ret = spi_c5_chunk(self, chunk, src, dest, poll_vm, &deadline);
            len -= chunk;
            if (src) { src += chunk; }
            if (dest) { dest += chunk; }
        }
        nlr_pop();
        spi_c5_stop(self, ret != 0);
        s->busy = false;
        if (ret != 0) { mp_raise_OSError(-ret); }
    } else {
        spi_c5_stop(self, true);
        s->busy = false;
        nlr_jump(nlr.ret_val);
    }
}

void spi_print(const mp_print_t *print, const spi_t *self, bool legacy) {
    (void)legacy;
    spi_state_t *s = self->spi;
    mp_printf(print, "SPI(%u", (unsigned)(self - spi_obj + 1));
    if (s->initialized) {
        mp_printf(print, ", baudrate=%u, polarity=%u, phase=%u, bits=%u", s->baudrate, s->polarity, s->phase, s->bits);
    }
    mp_print_str(print, ")");
}

const spi_t *spi_from_mp_obj(mp_obj_t obj) {
    if (!mp_obj_is_type(obj, &machine_spi_type)) { mp_raise_TypeError(MP_ERROR_TEXT("expecting an SPI object")); }
    return ((machine_hard_spi_obj_t *)MP_OBJ_TO_PTR(obj))->spi;
}

mp_obj_base_t *mp_hal_get_spi_obj(mp_obj_t obj) {
    if (mp_obj_is_type(obj, &machine_spi_type)) { return MP_OBJ_TO_PTR(obj); }
    #if MICROPY_PY_MACHINE_SOFTSPI
    if (mp_obj_is_type(obj, &mp_machine_soft_spi_type)) { return MP_OBJ_TO_PTR(obj); }
    #endif
    mp_raise_TypeError(MP_ERROR_TEXT("expecting an SPI object"));
}

static int spi_c5_proto_ioctl(void *self_in, uint32_t cmd) {
    spi_proto_cfg_t *self = self_in;
    if (cmd == MP_SPI_IOCTL_INIT) {
        spi_set_params(self->spi, 0xffffffff, self->baudrate, self->polarity, self->phase, self->bits, self->firstbit);
        return spi_init(self->spi, false);
    }
    if (cmd == MP_SPI_IOCTL_DEINIT) { spi_deinit(self->spi); }
    return 0;
}

static void spi_c5_proto_transfer(void *self_in, size_t len, const uint8_t *src, uint8_t *dest) {
    spi_proto_cfg_t *self = self_in;
    spi_transfer(self->spi, len, src, dest, SPI_TRANSFER_TIMEOUT(len));
}

const mp_spi_proto_t spi_proto = {.ioctl = spi_c5_proto_ioctl, .transfer = spi_c5_proto_transfer};
