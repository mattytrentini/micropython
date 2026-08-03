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

#include "hardware/spi.h"
#include "hardware/uart.h" // for uart_get_perclk()

#if MICROPY_PY_MACHINE_SPI

#define SPI_DEFAULT_BAUDRATE   (1000000)

// spi_write_blocking()/spi_write_read_blocking() cap len at 256 (the SDK's
// static spi_tx_buf/spi_rx_buf size) and silently do nothing if exceeded --
// raise a clear error instead of trusting the caller to know that.
#define SPI_MAX_XFER_LEN       (256)

typedef struct _machine_spi_obj_t {
    mp_obj_base_t base;
    uint8_t instance;
    uint8_t clkdiv;
    uint32_t baudrate;
} machine_spi_obj_t;

// Only instance 2 (SPI2: PC0=CLK, PC1=MOSI, PC2=MISO) is pin-muxed by the
// SDK on Dabao, so only one hardware SPI is usable.
static machine_spi_obj_t machine_spi_obj[1];

// SPI clock = perclk / (2 * (clkdiv + 1)); solve for clkdiv given a target
// baudrate.  uart_get_perclk() detects/caches the shared UDMA peripheral
// clock -- it's not actually UART-specific, just exposed via that header.
static uint8_t machine_spi_baudrate_to_clkdiv(uint32_t baudrate) {
    uint32_t clkdiv = uart_get_perclk() / (2 * baudrate);
    if (clkdiv > 0) {
        clkdiv -= 1;
    }
    if (clkdiv > 255) {
        clkdiv = 255;
    }
    return (uint8_t)clkdiv;
}

static void machine_spi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "SPI(%u, baudrate=%u, polarity=0, phase=0, bits=8, firstbit=MSB)",
        self->instance, self->baudrate);
}

// The SDK's command sequence hardcodes cpol=0/cpha=0/lsbfirst=0 -- SPI
// mode 0, MSB-first are the only settings actually achievable.
static void machine_spi_check_fixed_args(mp_int_t polarity, mp_int_t phase, mp_int_t bits, mp_int_t firstbit) {
    if (polarity > 0) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("only polarity=0 is supported"));
    }
    if (phase > 0) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("only phase=0 is supported"));
    }
    if (bits > 0 && bits != 8) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("only bits=8 is supported"));
    }
    if (firstbit > 0 && firstbit != MICROPY_PY_MACHINE_SPI_MSB) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("only firstbit=MSB is supported"));
    }
}

// machine.SPI(id=2, baudrate=1000000, polarity=0, phase=0, bits=8, firstbit=MSB)
static mp_obj_t machine_spi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    (void)type;
    enum { ARG_id, ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit, ARG_sck, ARG_mosi, ARG_miso };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,       MP_ARG_INT, {.u_int = 2} },
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = SPI_DEFAULT_BAUDRATE} },
        { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_phase,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = MICROPY_PY_MACHINE_SPI_MSB} },
        { MP_QSTR_sck,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_mosi,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_miso,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_id].u_int != 2) {
        // The SDK only pin-muxes instance 2; other instances have no
        // wired-up CLK/MOSI/MISO and would silently do nothing useful.
        mp_raise_msg_varg(&mp_type_NotImplementedError, MP_ERROR_TEXT("SPI(%d) is not wired up"), args[ARG_id].u_int);
    }
    if (args[ARG_sck].u_obj != MP_OBJ_NULL
        || args[ARG_mosi].u_obj != MP_OBJ_NULL
        || args[ARG_miso].u_obj != MP_OBJ_NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("explicit choice of sck/mosi/miso is not implemented"));
    }
    machine_spi_check_fixed_args(args[ARG_polarity].u_int, args[ARG_phase].u_int,
        args[ARG_bits].u_int, args[ARG_firstbit].u_int);

    machine_spi_obj_t *self = &machine_spi_obj[0];
    self->base.type = &machine_spi_type;
    self->instance = 2;
    self->baudrate = args[ARG_baudrate].u_int;
    self->clkdiv = machine_spi_baudrate_to_clkdiv(self->baudrate);
    spi_init(self->instance);
    return MP_OBJ_FROM_PTR(self);
}

static void machine_spi_init(mp_obj_base_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_spi_obj_t *self = (machine_spi_obj_t *)self_in;

    enum { ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_baudrate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_phase,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    machine_spi_check_fixed_args(args[ARG_polarity].u_int, args[ARG_phase].u_int,
        args[ARG_bits].u_int, args[ARG_firstbit].u_int);

    if (args[ARG_baudrate].u_int >= 0) {
        self->baudrate = args[ARG_baudrate].u_int;
        self->clkdiv = machine_spi_baudrate_to_clkdiv(self->baudrate);
    }
}

static void machine_spi_transfer(mp_obj_base_t *self_in, size_t len, const uint8_t *src, uint8_t *dest) {
    machine_spi_obj_t *self = (machine_spi_obj_t *)self_in;
    if (len > SPI_MAX_XFER_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("SPI transfer exceeds 256-byte SDK buffer"));
    }
    if (dest == NULL) {
        spi_write_blocking(self->instance, src, len, self->clkdiv);
    } else {
        spi_write_read_blocking(self->instance, src, dest, len, self->clkdiv);
    }
}

static const mp_machine_spi_p_t machine_spi_p = {
    .init = machine_spi_init,
    .deinit = NULL,
    .transfer = machine_spi_transfer,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_spi_type,
    MP_QSTR_SPI,
    MP_TYPE_FLAG_NONE,
    make_new, machine_spi_make_new,
    print, machine_spi_print,
    protocol, &machine_spi_p,
    locals_dict, &mp_machine_spi_locals_dict
    );

#endif // MICROPY_PY_MACHINE_SPI
