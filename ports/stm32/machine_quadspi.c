/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Matt Trentini
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

// machine.QuadSPI / machine.OctoSPI backend for the OCTOSPI1 peripheral.
//
// OCTOSPI1 (on H5 this is register-aliased from ST's newer "XSPI" naming,
// see stm32h5xx.h's `typedef XSPI_TypeDef OCTOSPI_TypeDef`) can run its data
// phase on 1, 2, 4 or 8 lines, so a single peripheral backs both machine.QuadSPI
// (4 lines) and machine.OctoSPI (8 lines) -- they can't be used at the same
// time, tracked via `octospi1_owner` below.
//
// This deliberately bypasses ST's HAL_OSPI_*/HAL_XSPI_* driver and drives the
// peripheral with bare register access instead, exactly like the existing
// narrow NOR-flash driver in octospi.c (which this file's register recipe is
// based on). Unlike octospi.c, transfers here have no instruction/address/
// alternate-byte phase at all -- write()/read() are pure N-line data
// transfers, matching the machine.SPI-like API contract shared with the ESP32
// backend. Because OCTOSPI1's NCS pin is hardware-managed by the peripheral
// around each indirect-mode operation (unlike ESP32, where hardware CS is
// disabled and the caller drives a machine.Pin), each write() or read() call
// here is its own complete, separately-CS-pulsed transaction -- see the note
// in docs/library/machine.QuadSPI.rst.
//
// As with machine.SPI on this port, pins are fixed by board configuration
// (MICROPY_HW_OCTOSPI1_SCK/NCS/IO0-IO7) -- explicit sck=/io0=/etc constructor
// kwargs are not implemented, matching ports/stm32/machine_spi.c.

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "extmod/modmachine.h"

#if MICROPY_PY_MACHINE_QUADSPI || MICROPY_PY_MACHINE_OCTOSPI

#include "pin_static_af.h"
#include "dma.h"

#ifndef MICROPY_HW_OCTOSPI_PRESCALER_MIN
#define MICROPY_HW_OCTOSPI_PRESCALER_MIN (1)
#endif

#ifndef MICROPY_HW_OCTOSPI_CS_HIGH_CYCLES
#define MICROPY_HW_OCTOSPI_CS_HIGH_CYCLES (2) // nCS stays high for 2 cycles
#endif

typedef struct _machine_octospi_obj_t {
    mp_obj_base_t base;
    uint8_t num_lines; // 4 for machine.QuadSPI, 8 for machine.OctoSPI
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    uint8_t firstbit;
    enum {
        MACHINE_OCTOSPI_STATE_NONE,
        MACHINE_OCTOSPI_STATE_INIT,
        MACHINE_OCTOSPI_STATE_DEINIT,
    } state;
} machine_octospi_obj_t;

#if MICROPY_PY_MACHINE_QUADSPI
static machine_octospi_obj_t machine_quadspi_obj_instance;
#endif
#if MICROPY_PY_MACHINE_OCTOSPI
static machine_octospi_obj_t machine_octospi_obj_instance;
#endif

// Only one of machine.QuadSPI(1)/machine.OctoSPI(1) can own the single
// physical OCTOSPI1 peripheral at a time.
static machine_octospi_obj_t *octospi1_owner = NULL;

// H7 routes OCTOSPI pins through an extra I/O manager (OCTOSPIM), so its
// static AF macros are named differently; every other OCTOSPI1-capable
// family (H5, L4, U5) uses the plain STATIC_AF_OCTOSPI1_* names. Same
// selection octospi.c already uses for the narrow flash-command driver.
#if defined(STM32H7)
#define STATIC_AF_OCTOSPI(signal) STATIC_AF_OCTOSPIM_P1_##signal
#else
#define STATIC_AF_OCTOSPI(signal) STATIC_AF_OCTOSPI1_##signal
#endif

static void machine_octospi1_pins_init(uint8_t num_lines) {
    mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_NCS, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(NCS));
    mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_SCK, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(CLK));
    mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_IO0, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(IO0));
    mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_IO1, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(IO1));
    mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_IO2, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(IO2));
    mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_IO3, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(IO3));
    #if MICROPY_PY_MACHINE_OCTOSPI
    if (num_lines == 8) {
        mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_IO4, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(IO4));
        mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_IO5, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(IO5));
        mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_IO6, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(IO6));
        mp_hal_pin_config_alt_static_speed(MICROPY_HW_OCTOSPI1_IO7, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_NONE, MP_HAL_PIN_SPEED_VERY_HIGH, STATIC_AF_OCTOSPI(IO7));
    }
    #else
    (void)num_lines;
    #endif
}

// Compute the DCR2 PRESCALER field value (1..256) giving an OCTOSPI kernel
// clock at or below the requested baudrate. Assumes the OCTOSPI1 kernel
// clock equals HCLK (the default RCC mux setting; a board that changes this
// via RCC_CCIPR would need its own adjustment).
static uint32_t machine_octospi_prescaler_for_baudrate(uint32_t baudrate) {
    uint32_t hclk = HAL_RCC_GetHCLKFreq();
    uint32_t prescaler = (hclk + baudrate - 1) / baudrate;
    if (prescaler < MICROPY_HW_OCTOSPI_PRESCALER_MIN) {
        prescaler = MICROPY_HW_OCTOSPI_PRESCALER_MIN;
    } else if (prescaler > 256) {
        prescaler = 256;
    }
    return prescaler;
}

static uint32_t machine_octospi_actual_baudrate(uint32_t prescaler) {
    return HAL_RCC_GetHCLKFreq() / prescaler;
}

// Shared argument layout for both classes' init() and make_new(); io4-io7
// are only meaningful for machine.OctoSPI, but keeping one shared enum
// avoids the args[] index mismatches that come from each class declaring
// its own locally-renumbered enum.
enum { ARG_id, ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit,
       ARG_sck, ARG_io0, ARG_io1, ARG_io2, ARG_io3, ARG_io4, ARG_io5, ARG_io6, ARG_io7 };

static void machine_octospi_reject_pin_kwargs(size_t n_args_from_sck, const mp_arg_val_t *pin_args) {
    for (size_t i = 0; i < n_args_from_sck; i++) {
        if (pin_args[i].u_obj != MP_OBJ_NULL) {
            mp_raise_ValueError(MP_ERROR_TEXT("explicit choice of pins is not implemented"));
        }
    }
}

static void machine_octospi_init_internal(machine_octospi_obj_t *self, uint8_t num_lines, mp_arg_val_t args[]) {
    bool changed = self->state != MACHINE_OCTOSPI_STATE_INIT;

    uint32_t baudrate = self->baudrate;
    if (args[ARG_baudrate].u_int != -1) {
        baudrate = args[ARG_baudrate].u_int;
    }
    uint32_t prescaler = machine_octospi_prescaler_for_baudrate(baudrate);
    uint32_t actual_baudrate = machine_octospi_actual_baudrate(prescaler);
    if (actual_baudrate != self->baudrate) {
        self->baudrate = actual_baudrate;
        changed = true;
    }

    if (args[ARG_polarity].u_int != -1 && args[ARG_polarity].u_int != self->polarity) {
        self->polarity = args[ARG_polarity].u_int;
        changed = true;
    }

    if (args[ARG_phase].u_int != -1 && args[ARG_phase].u_int != self->phase) {
        self->phase = args[ARG_phase].u_int;
        changed = true;
    }

    if (args[ARG_bits].u_int != -1 && args[ARG_bits].u_int <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid bits"));
    }
    if (args[ARG_bits].u_int != -1 && args[ARG_bits].u_int != self->bits) {
        self->bits = args[ARG_bits].u_int;
        changed = true;
    }

    if (args[ARG_firstbit].u_int != -1) {
        if (args[ARG_firstbit].u_int != MICROPY_PY_MACHINE_SPI_MSB) {
            mp_raise_ValueError(MP_ERROR_TEXT("firstbit must be MSB"));
        }
        if (args[ARG_firstbit].u_int != self->firstbit) {
            self->firstbit = args[ARG_firstbit].u_int;
            changed = true;
        }
    }

    if (!changed) {
        return;
    }

    if (octospi1_owner != NULL && octospi1_owner != self) {
        mp_raise_OSError(MP_EBUSY);
    }

    self->num_lines = num_lines;

    machine_octospi1_pins_init(num_lines);

    __HAL_RCC_OSPI1_CLK_ENABLE();
    __HAL_RCC_OSPI1_FORCE_RESET();
    __HAL_RCC_OSPI1_RELEASE_RESET();

    OCTOSPI1->CR =
        3 << OCTOSPI_CR_FTHRES_Pos // 4 bytes must be available to read/write
            | 0 << OCTOSPI_CR_MSEL_Pos // FLASH 0 selected
            | 0 << OCTOSPI_CR_DMM_Pos // dual-memory mode disabled
    ;

    OCTOSPI1->DCR1 =
        0 << OCTOSPI_DCR1_DEVSIZE_Pos // unused: no address phase is ever generated
            | (MICROPY_HW_OCTOSPI_CS_HIGH_CYCLES - 1) << OCTOSPI_DCR1_CSHT_Pos
            | 0 << OCTOSPI_DCR1_CKMODE_Pos // CLK idles at low state
    ;

    OCTOSPI1->DCR2 = (prescaler - 1) << OCTOSPI_DCR2_PRESCALER_Pos;
    OCTOSPI1->DCR3 = 0;
    OCTOSPI1->DCR4 = 0;

    OCTOSPI1->CR |= OCTOSPI_CR_EN;

    self->state = MACHINE_OCTOSPI_STATE_INIT;
    octospi1_owner = self;
}

static void machine_octospi_deinit_internal(machine_octospi_obj_t *self) {
    if (OCTOSPI1->SR & OCTOSPI_SR_BUSY) {
        OCTOSPI1->CR |= OCTOSPI_CR_ABORT;
        while (OCTOSPI1->CR & OCTOSPI_CR_ABORT) {
        }
    }
    OCTOSPI1->CR &= ~OCTOSPI_CR_EN;
    if (octospi1_owner == self) {
        octospi1_owner = NULL;
    }
}

static void machine_octospi_deinit(mp_obj_base_t *self_in) {
    machine_octospi_obj_t *self = (machine_octospi_obj_t *)self_in;
    if (self->state == MACHINE_OCTOSPI_STATE_INIT) {
        self->state = MACHINE_OCTOSPI_STATE_DEINIT;
        machine_octospi_deinit_internal(self);
    }
}

// Pure N-line data-phase transfer: no instruction/address/alternate-byte
// phase, so writing CCR (with IMODE=ADMODE=ABMODE=0) is what triggers the
// start of the operation (per the OCTOSPI indirect-mode trigger rules: the
// operation starts on the write to IR if used, else AR if used, else CCR).
static void machine_octospi_transfer_one_way(machine_octospi_obj_t *self, size_t len, const uint8_t *src, uint8_t *dest) {
    if (len == 0) {
        return;
    }

    uint32_t dmode = self->num_lines == 8 ? 4 : 3; // CCR DMODE: 4-line=3, 8-line=4

    OCTOSPI1->FCR = OCTOSPI_FCR_CTCF; // clear TC flag
    OCTOSPI1->CR = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE_Msk) | (src != NULL ? 0U : 1U) << OCTOSPI_CR_FMODE_Pos; // indirect write/read
    OCTOSPI1->DLR = len - 1;
    OCTOSPI1->TCR = 0 << OCTOSPI_TCR_DCYC_Pos; // 0 dummy cycles

    #if defined(STM32H5)
    // DMA-driven transfer via GPDMA (no HAL_OSPI/XSPI driver is used, so this
    // is done directly with dma_init()/HAL_DMA_Start() rather than a
    // HAL_OSPI_*_DMA-style helper).
    DMA_HandleTypeDef dma;
    dma_init(&dma, &dma_OCTOSPI_1, src != NULL ? DMA_MEMORY_TO_PERIPH : DMA_PERIPH_TO_MEMORY, NULL);
    if (src != NULL) {
        HAL_DMA_Start(&dma, (uint32_t)src, (uint32_t)&OCTOSPI1->DR, len);
    } else {
        HAL_DMA_Start(&dma, (uint32_t)&OCTOSPI1->DR, (uint32_t)dest, len);
    }
    OCTOSPI1->CR |= OCTOSPI_CR_DMAEN;

    OCTOSPI1->CCR = dmode << OCTOSPI_CCR_DMODE_Pos; // triggers the start of the operation

    while (!(OCTOSPI1->SR & OCTOSPI_SR_TCF)) {
        if (OCTOSPI1->SR & OCTOSPI_SR_TEF) {
            break;
        }
    }

    OCTOSPI1->CR &= ~OCTOSPI_CR_DMAEN;
    dma_deinit(&dma_OCTOSPI_1);
    #else
    // Polling fallback for OCTOSPI1-capable families without a DMA
    // descriptor defined yet (see dma.c) -- byte-at-a-time, same technique
    // octospi.c already uses for its narrow flash-command protocol.
    OCTOSPI1->CCR = dmode << OCTOSPI_CCR_DMODE_Pos; // triggers the start of the operation

    if (src != NULL) {
        size_t remaining = len;
        while (remaining) {
            while (!(OCTOSPI1->SR & OCTOSPI_SR_FTF)) {
                if (OCTOSPI1->SR & OCTOSPI_SR_TEF) {
                    goto transfer_done;
                }
            }
            *(volatile uint8_t *)&OCTOSPI1->DR = *src++;
            --remaining;
        }
    } else {
        size_t remaining = len;
        while (remaining) {
            while (!((OCTOSPI1->SR >> OCTOSPI_SR_FLEVEL_Pos) & 0x3f)) {
                if (OCTOSPI1->SR & OCTOSPI_SR_TEF) {
                    goto transfer_done;
                }
            }
            *dest++ = *(volatile uint8_t *)&OCTOSPI1->DR;
            --remaining;
        }
    }

    while (!(OCTOSPI1->SR & OCTOSPI_SR_TCF)) {
        if (OCTOSPI1->SR & OCTOSPI_SR_TEF) {
            break;
        }
    }
    transfer_done:
    #endif

    OCTOSPI1->FCR = OCTOSPI_FCR_CTCF; // clear TC flag
}

static void machine_octospi_write(mp_obj_base_t *self_in, size_t len, const uint8_t *src) {
    machine_octospi_obj_t *self = (machine_octospi_obj_t *)self_in;
    if (self->state == MACHINE_OCTOSPI_STATE_DEINIT) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("write on deinitialized bus"));
        return;
    }
    machine_octospi_transfer_one_way(self, len, src, NULL);
}

static void machine_octospi_read(mp_obj_base_t *self_in, size_t len, uint8_t *dest) {
    machine_octospi_obj_t *self = (machine_octospi_obj_t *)self_in;
    if (self->state == MACHINE_OCTOSPI_STATE_DEINIT) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("read on deinitialized bus"));
        return;
    }
    machine_octospi_transfer_one_way(self, len, NULL, dest);
}

#if MICROPY_PY_MACHINE_QUADSPI

static const mp_arg_t quadspi_allowed_args[] = {
    { MP_QSTR_id,       MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_phase,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_sck,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io0,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io1,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io2,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io3,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
};

static void machine_quadspi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_octospi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "QuadSPI(id=1, baudrate=%u, polarity=%u, phase=%u, bits=%u, firstbit=%u)",
        self->baudrate, self->polarity, self->phase, self->bits, self->firstbit);
}

static void machine_quadspi_init(mp_obj_base_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_octospi_obj_t *self = (machine_octospi_obj_t *)self_in;

    mp_arg_val_t args[MP_ARRAY_SIZE(quadspi_allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(quadspi_allowed_args) - 1,
        quadspi_allowed_args + 1, args + 1);

    machine_octospi_reject_pin_kwargs(ARG_io3 - ARG_sck + 1, &args[ARG_sck]);
    machine_octospi_init_internal(self, 4, args);
}

static mp_obj_t machine_quadspi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(quadspi_allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(quadspi_allowed_args), quadspi_allowed_args, args);

    if (args[ARG_id].u_int != 1) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("QuadSPI(%d) doesn't exist"), args[ARG_id].u_int);
    }

    machine_octospi_reject_pin_kwargs(ARG_io3 - ARG_sck + 1, &args[ARG_sck]);

    static const mp_int_t defaults[] = { 1000000, 0, 0, 8, MICROPY_PY_MACHINE_SPI_MSB };
    for (int i = ARG_baudrate; i <= ARG_firstbit; i++) {
        if (args[i].u_int == -1) {
            args[i].u_int = defaults[i - ARG_baudrate];
        }
    }

    machine_octospi_obj_t *self = &machine_quadspi_obj_instance;
    self->base.type = &machine_quadspi_type;

    machine_octospi_init_internal(self, 4, args);

    return MP_OBJ_FROM_PTR(self);
}

static const mp_machine_quadspi_p_t machine_quadspi_p = {
    .init = machine_quadspi_init,
    .deinit = machine_octospi_deinit,
    .write = machine_octospi_write,
    .read = machine_octospi_read,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_quadspi_type,
    MP_QSTR_QuadSPI,
    MP_TYPE_FLAG_NONE,
    make_new, machine_quadspi_make_new,
    print, machine_quadspi_print,
    protocol, &machine_quadspi_p,
    locals_dict, &mp_machine_quadspi_locals_dict
    );

#endif // MICROPY_PY_MACHINE_QUADSPI

#if MICROPY_PY_MACHINE_OCTOSPI

static const mp_arg_t octospi_allowed_args[] = {
    { MP_QSTR_id,       MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_phase,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_sck,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io0,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io1,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io2,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io3,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io4,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io5,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io6,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io7,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
};

static void machine_octospi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_octospi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "OctoSPI(id=1, baudrate=%u, polarity=%u, phase=%u, bits=%u, firstbit=%u)",
        self->baudrate, self->polarity, self->phase, self->bits, self->firstbit);
}

static void machine_octospi_init(mp_obj_base_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_octospi_obj_t *self = (machine_octospi_obj_t *)self_in;

    mp_arg_val_t args[MP_ARRAY_SIZE(octospi_allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(octospi_allowed_args) - 1,
        octospi_allowed_args + 1, args + 1);

    machine_octospi_reject_pin_kwargs(ARG_io7 - ARG_sck + 1, &args[ARG_sck]);
    machine_octospi_init_internal(self, 8, args);
}

static mp_obj_t machine_octospi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(octospi_allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(octospi_allowed_args), octospi_allowed_args, args);

    if (args[ARG_id].u_int != 1) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("OctoSPI(%d) doesn't exist"), args[ARG_id].u_int);
    }

    machine_octospi_reject_pin_kwargs(ARG_io7 - ARG_sck + 1, &args[ARG_sck]);

    static const mp_int_t defaults[] = { 1000000, 0, 0, 8, MICROPY_PY_MACHINE_SPI_MSB };
    for (int i = ARG_baudrate; i <= ARG_firstbit; i++) {
        if (args[i].u_int == -1) {
            args[i].u_int = defaults[i - ARG_baudrate];
        }
    }

    machine_octospi_obj_t *self = &machine_octospi_obj_instance;
    self->base.type = &machine_octospi_type;

    machine_octospi_init_internal(self, 8, args);

    return MP_OBJ_FROM_PTR(self);
}

static const mp_machine_quadspi_p_t machine_octospi_p = {
    .init = machine_octospi_init,
    .deinit = machine_octospi_deinit,
    .write = machine_octospi_write,
    .read = machine_octospi_read,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_octospi_type,
    MP_QSTR_OctoSPI,
    MP_TYPE_FLAG_NONE,
    make_new, machine_octospi_make_new,
    print, machine_octospi_print,
    protocol, &machine_octospi_p,
    locals_dict, &mp_machine_quadspi_locals_dict
    );

#endif // MICROPY_PY_MACHINE_OCTOSPI

#endif // MICROPY_PY_MACHINE_QUADSPI || MICROPY_PY_MACHINE_OCTOSPI
