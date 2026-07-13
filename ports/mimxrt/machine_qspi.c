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

// machine.QuadSPI backend for i.MXRT, using the FlexSPI peripheral's Port B.
//
// i.MXRT chips of this class have no internal flash: the running firmware is
// itself fetched (XIP) from external QSPI/octal NOR flash via FlexSPI Port A
// (see ports/mimxrt/flash.c, hal/flexspi_flash_config.h). There's only one
// FlexSPI controller, and its IP-command interface and AHB/XIP-fetch
// interface share the same physical bus/sequencer -- issuing ANY IP command
// (even one targeting Port B, as this driver does) stalls ongoing AHB
// fetches from Port A while it's in flight. Concretely, this means every
// write()/read() here must run from RAM (not flash) with global interrupts
// and the D-cache disabled for its duration -- otherwise an interrupt (or
// the CPU's own next-instruction fetch) trying to read code from the boot
// flash mid-transfer would hang. This mirrors exactly how
// ports/mimxrt/flash.c already handles its own erase/write operations on
// the same shared bus.
//
// Because of the above, this uses FLEXSPI_TransferBlocking rather than
// FlexSPI's eDMA transfer API: eDMA's usual benefit is asynchronous
// completion via an interrupt callback, which doesn't fit here since
// interrupts must stay disabled for the whole transfer regardless.
//
// Only machine.QuadSPI is implemented, not machine.OctoSPI: RT1011's single
// FlexSPI controller has two 4-line ports (A and B), not a single 8-line
// port -- there's no genuine single-chip octal mode here, only a
// port-combination trick (MCR0 COMBINATIONEN) that needs both ports wired to
// one flash/PSRAM device, a non-standard setup this driver doesn't attempt.
//
// As with machine.SPI on this port, pins are fixed by board configuration
// (IOMUX_TABLE_QSPI) -- explicit sck=/io0=/etc constructor kwargs are not
// implemented.

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "extmod/modmachine.h"
#include CLOCK_CONFIG_H

#include "fsl_cache.h"
#include "fsl_iomuxc.h"
#include "fsl_flexspi.h"
#include "pin.h"
#include "flash.h" // for BOARD_FLEX_SPI

#if MICROPY_PY_MACHINE_QUADSPI

// Custom LUT sequence indices for our data-only quad transfers. FlexSPI has
// 16 sequence slots (4 LUT registers each, 64 total); the boot flash's own
// config (hal/flexspi_flash_config.h) uses indices 0-11 and 13, leaving 14
// and 15 free.
#define QSPI_LUT_SEQ_IDX_WRITE (14)
#define QSPI_LUT_SEQ_IDX_READ  (15)

#define QSPI_LUT_SEQ(cmd0, pad0, op0, cmd1, pad1, op1) \
    (FLEXSPI_LUT_OPERAND0(op0) | FLEXSPI_LUT_NUM_PADS0(pad0) | FLEXSPI_LUT_OPCODE0(cmd0) | \
        FLEXSPI_LUT_OPERAND1(op1) | FLEXSPI_LUT_NUM_PADS1(pad1) | FLEXSPI_LUT_OPCODE1(cmd1))

typedef struct _iomux_table_t {
    uint32_t muxRegister;
    uint32_t muxMode;
    uint32_t inputRegister;
    uint32_t inputDaisy;
    uint32_t configRegister;
} iomux_table_t;

static const iomux_table_t iomux_table_qspi[] = {
    IOMUX_TABLE_QSPI
};

#define QSPI_SCLK  (iomux_table_qspi[0])
#define QSPI_SS0   (iomux_table_qspi[1])
#define QSPI_DATA0 (iomux_table_qspi[2])
#define QSPI_DATA1 (iomux_table_qspi[3])
#define QSPI_DATA2 (iomux_table_qspi[4])
#define QSPI_DATA3 (iomux_table_qspi[5])

typedef struct _machine_qspi_obj_t {
    mp_obj_base_t base;
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    uint8_t firstbit;
    enum {
        MACHINE_QSPI_STATE_NONE,
        MACHINE_QSPI_STATE_INIT,
        MACHINE_QSPI_STATE_DEINIT,
    } state;
} machine_qspi_obj_t;

static machine_qspi_obj_t machine_qspi_obj_instance;

static void machine_qspi_set_iomux(uint8_t drive) {
    const iomux_table_t *pins[] = { &QSPI_SCLK, &QSPI_SS0, &QSPI_DATA0, &QSPI_DATA1, &QSPI_DATA2, &QSPI_DATA3 };
    for (size_t i = 0; i < MP_ARRAY_SIZE(pins); i++) {
        const iomux_table_t *p = pins[i];
        IOMUXC_SetPinMux(p->muxRegister, p->muxMode, p->inputRegister, p->inputDaisy, p->configRegister, 0U);
        IOMUXC_SetPinConfig(p->muxRegister, p->muxMode, p->inputRegister, p->inputDaisy, p->configRegister,
            pin_generate_config(PIN_PULL_UP_100K, PIN_MODE_OUT, drive, p->configRegister));
    }
}

static uint32_t machine_qspi_baudrate_divider(uint32_t baudrate) {
    uint32_t divider = (BOARD_BOOTCLOCKRUN_FLEXSPI_CLK_ROOT + baudrate - 1) / baudrate;
    if (divider < 1) {
        divider = 1;
    } else if (divider > 32) {
        divider = 32;
    }
    return divider;
}

static void machine_qspi_init_internal(machine_qspi_obj_t *self, mp_arg_val_t args[]) {
    enum { ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit, ARG_drive };

    if (args[ARG_baudrate].u_int != -1) {
        self->baudrate = args[ARG_baudrate].u_int;
    }
    if (args[ARG_polarity].u_int != -1) {
        self->polarity = args[ARG_polarity].u_int;
    }
    if (args[ARG_phase].u_int != -1) {
        self->phase = args[ARG_phase].u_int;
    }
    if (args[ARG_bits].u_int != -1) {
        if (args[ARG_bits].u_int <= 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid bits"));
        }
        self->bits = args[ARG_bits].u_int;
    }
    if (args[ARG_firstbit].u_int != -1) {
        if (args[ARG_firstbit].u_int != MICROPY_PY_MACHINE_SPI_MSB) {
            mp_raise_ValueError(MP_ERROR_TEXT("firstbit must be MSB"));
        }
        self->firstbit = args[ARG_firstbit].u_int;
    }
    uint8_t drive = args[ARG_drive].u_int;
    if (drive < 1 || drive > 7) {
        drive = 6;
    }

    machine_qspi_set_iomux(drive);

    // Install our data-only LUT sequences. This only touches slots 14/15,
    // leaving the boot flash's sequences (0-11, 13) on Port A untouched.
    uint32_t lut_write[4] = {
        QSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_4PAD, 0x00,
            kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00),
        0, 0, 0,
    };
    uint32_t lut_read[4] = {
        QSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR, kFLEXSPI_4PAD, 0x00,
            kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00),
        0, 0, 0,
    };
    FLEXSPI_UpdateLUT(BOARD_FLEX_SPI, QSPI_LUT_SEQ_IDX_WRITE * 4, lut_write, 4);
    FLEXSPI_UpdateLUT(BOARD_FLEX_SPI, QSPI_LUT_SEQ_IDX_READ * 4, lut_read, 4);

    // Configure Port B's device timing. This only affects Port B; Port A's
    // (the boot flash's) device config, set up separately by
    // hal/flexspi_nor_flash.c, is untouched.
    flexspi_device_config_t device_config = {
        .flexspiRootClk = BOARD_BOOTCLOCKRUN_FLEXSPI_CLK_ROOT,
        .flashSize = 0x4000, // 16MB address window; arbitrary, no AHB access is used on Port B
        .CSIntervalUnit = kFLEXSPI_CsIntervalUnit1SckCycle,
        .CSInterval = 2,
        .CSHoldTime = 3,
        .CSSetupTime = 3,
        .columnspace = 0,
        .enableWordAddress = false,
        .AWRSeqIndex = 0,
        .AWRSeqNumber = 0,
        .ARDSeqIndex = 0,
        .ARDSeqNumber = 0,
        .AHBWriteWaitUnit = kFLEXSPI_AhbWriteWaitUnit2AhbCycle,
        .AHBWriteWaitInterval = 0,
        .enableWriteMask = false,
    };
    // Divider from the requested baudrate; actual clock may be lower.
    uint32_t divider = machine_qspi_baudrate_divider(self->baudrate);
    self->baudrate = BOARD_BOOTCLOCKRUN_FLEXSPI_CLK_ROOT / divider;
    FLEXSPI_SetFlashConfig(BOARD_FLEX_SPI, &device_config, kFLEXSPI_PortB1);

    self->state = MACHINE_QSPI_STATE_INIT;
}

static void machine_qspi_deinit(mp_obj_base_t *self_in) {
    machine_qspi_obj_t *self = (machine_qspi_obj_t *)self_in;
    self->state = MACHINE_QSPI_STATE_DEINIT;
}

// Runs from RAM with interrupts and the D-cache disabled: FlexSPI's IP
// command interface and its AHB/XIP-fetch interface (currently serving code
// execution from Port A) share the same bus, so nothing may try to fetch an
// instruction from flash while this is in flight.
__attribute__((section(".ram_functions")))
static void machine_qspi_transfer_one_way(size_t len, const uint8_t *src, uint8_t *dest) {
    flexspi_transfer_t transfer = {
        .deviceAddress = 0,
        .port = kFLEXSPI_PortB1,
        .cmdType = src != NULL ? kFLEXSPI_Write : kFLEXSPI_Read,
        .seqIndex = src != NULL ? QSPI_LUT_SEQ_IDX_WRITE : QSPI_LUT_SEQ_IDX_READ,
        .SeqNumber = 1,
        .data = (uint32_t *)(src != NULL ? (const void *)src : (void *)dest),
        .dataSize = len,
    };

    __disable_irq();
    SCB_DisableDCache();

    FLEXSPI_TransferBlocking(BOARD_FLEX_SPI, &transfer);

    SCB_EnableDCache();
    __enable_irq();
}

static void machine_qspi_write(mp_obj_base_t *self_in, size_t len, const uint8_t *src) {
    machine_qspi_obj_t *self = (machine_qspi_obj_t *)self_in;
    if (self->state == MACHINE_QSPI_STATE_DEINIT) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("write on deinitialized bus"));
        return;
    }
    if (len == 0) {
        return;
    }
    machine_qspi_transfer_one_way(len, src, NULL);
}

static void machine_qspi_read(mp_obj_base_t *self_in, size_t len, uint8_t *dest) {
    machine_qspi_obj_t *self = (machine_qspi_obj_t *)self_in;
    if (self->state == MACHINE_QSPI_STATE_DEINIT) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("read on deinitialized bus"));
        return;
    }
    if (len == 0) {
        return;
    }
    machine_qspi_transfer_one_way(len, NULL, dest);
}

static void machine_qspi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_qspi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "QuadSPI(id=1, baudrate=%u, polarity=%u, phase=%u, bits=%u, firstbit=%u)",
        self->baudrate, self->polarity, self->phase, self->bits, self->firstbit);
}

enum { ARG_id, ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit, ARG_drive,
       ARG_sck, ARG_io0, ARG_io1, ARG_io2, ARG_io3 };
static const mp_arg_t quadspi_allowed_args[] = {
    { MP_QSTR_id,       MP_ARG_INT, {.u_int = 1} },
    { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_phase,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_drive,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_sck,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io0,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io1,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io2,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_io3,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
};

static void machine_qspi_reject_pin_kwargs(mp_arg_val_t args[]) {
    for (int i = ARG_sck; i <= ARG_io3; i++) {
        if (args[i].u_obj != MP_OBJ_NULL) {
            mp_raise_ValueError(MP_ERROR_TEXT("explicit choice of pins is not implemented"));
        }
    }
}

static void machine_qspi_init(mp_obj_base_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_qspi_obj_t *self = (machine_qspi_obj_t *)self_in;

    mp_arg_val_t args[MP_ARRAY_SIZE(quadspi_allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(quadspi_allowed_args) - 1,
        quadspi_allowed_args + 1, args + 1);

    machine_qspi_reject_pin_kwargs(args);
    machine_qspi_init_internal(self, args + 1);
}

static mp_obj_t machine_qspi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(quadspi_allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(quadspi_allowed_args), quadspi_allowed_args, args);

    if (args[ARG_id].u_int != 1) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("QuadSPI(%d) doesn't exist"), args[ARG_id].u_int);
    }

    machine_qspi_reject_pin_kwargs(args);

    static const mp_int_t defaults[] = { 1000000, 0, 0, 8, MICROPY_PY_MACHINE_SPI_MSB };
    for (int i = ARG_baudrate; i <= ARG_firstbit; i++) {
        if (args[i].u_int == -1) {
            args[i].u_int = defaults[i - ARG_baudrate];
        }
    }

    machine_qspi_obj_t *self = &machine_qspi_obj_instance;
    self->base.type = &machine_quadspi_type;

    machine_qspi_init_internal(self, args + 1);

    return MP_OBJ_FROM_PTR(self);
}

static const mp_machine_quadspi_p_t machine_qspi_p = {
    .init = machine_qspi_init,
    .deinit = machine_qspi_deinit,
    .write = machine_qspi_write,
    .read = machine_qspi_read,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_quadspi_type,
    MP_QSTR_QuadSPI,
    MP_TYPE_FLAG_NONE,
    make_new, machine_qspi_make_new,
    print, machine_qspi_print,
    protocol, &machine_qspi_p,
    locals_dict, &mp_machine_quadspi_locals_dict
    );

#endif // MICROPY_PY_MACHINE_QUADSPI
