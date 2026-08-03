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

#include <string.h>

#include "py/mperrno.h"
#include "py/runtime.h"

#include "extmod/modmachine.h"

#include "hardware/i2c.h"

#if MICROPY_PY_MACHINE_I2C

#define I2C_DEFAULT_FREQ    (400000)

// The SDK's i2c_tx_buf/i2c_rx_buf are static 64-byte arrays with no bounds
// checking of their own -- a longer transfer would silently overflow into
// adjacent static memory.  Enforce the limit here instead.
#define I2C_MAX_XFER_LEN    (64)

typedef struct _machine_i2c_obj_t {
    mp_obj_base_t base;
    uint8_t instance;
    uint32_t freq;
} machine_i2c_obj_t;

// Only instance 0 (I2C0: PB11=SCL, PB12=SDA) is pin-muxed by the SDK on
// Dabao, so only one hardware I2C is usable.
static machine_i2c_obj_t machine_i2c_obj[1];

static void check_xfer_len(size_t len) {
    if (len > I2C_MAX_XFER_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("I2C transfer exceeds 64-byte SDK buffer"));
    }
}

// NOTE: the SDK never reports slave NACKs (UDMA_I2C's status register is
// never read back by i2c_write_blocking/read_blocking/write_read_blocking),
// so every transfer below "succeeds" even if no device is present at addr.
// This means machine.I2C.scan() cannot distinguish real devices from empty
// addresses, and writeto()'s return value can't be trusted as an ack count.
static int machine_i2c_transfer(mp_obj_base_t *self_in, uint16_t addr, size_t n, mp_machine_i2c_buf_t *bufs, unsigned int flags) {
    machine_i2c_obj_t *self = (machine_i2c_obj_t *)self_in;
    uint8_t tx_buf[I2C_MAX_XFER_LEN];
    uint8_t rx_buf[I2C_MAX_XFER_LEN];

    #if MICROPY_PY_MACHINE_I2C_TRANSFER_WRITE1
    if (flags & MP_MACHINE_I2C_FLAG_WRITE1) {
        // bufs[0] is a write (e.g. a register address); bufs[1..n-1] are
        // concatenated into a single read.  Only extmod's readfrom_mem()/
        // readfrom_mem_into() use this, always with n == 2.
        size_t rx_len = 0;
        for (size_t i = 1; i < n; ++i) {
            rx_len += bufs[i].len;
        }
        check_xfer_len(bufs[0].len);
        check_xfer_len(rx_len);
        int ret = i2c_write_read_blocking(self->instance, addr, bufs[0].buf, bufs[0].len, rx_buf, rx_len);
        if (ret != 0) {
            return -MP_EIO;
        }
        size_t off = 0;
        for (size_t i = 1; i < n; ++i) {
            memcpy(bufs[i].buf, rx_buf + off, bufs[i].len);
            off += bufs[i].len;
        }
        return bufs[0].len + rx_len;
    }
    #endif

    size_t len = 0;
    for (size_t i = 0; i < n; ++i) {
        len += bufs[i].len;
    }
    check_xfer_len(len);

    int ret;
    if (flags & MP_MACHINE_I2C_FLAG_READ) {
        ret = i2c_read_blocking(self->instance, addr, rx_buf, len);
        if (ret == 0) {
            size_t off = 0;
            for (size_t i = 0; i < n; ++i) {
                memcpy(bufs[i].buf, rx_buf + off, bufs[i].len);
                off += bufs[i].len;
            }
        }
    } else {
        size_t off = 0;
        for (size_t i = 0; i < n; ++i) {
            memcpy(tx_buf + off, bufs[i].buf, bufs[i].len);
            off += bufs[i].len;
        }
        ret = i2c_write_blocking(self->instance, addr, tx_buf, len);
    }

    return (ret == 0) ? (int)len : -MP_EIO;
}

static void machine_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "I2C(%u, freq=%u)", self->instance, self->freq);
}

// machine.I2C(id=0, *, freq=400000)
static mp_obj_t machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    (void)type;
    enum { ARG_id, ARG_scl, ARG_sda, ARG_freq };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,  MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_scl, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_sda, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_freq, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = I2C_DEFAULT_FREQ} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_id].u_int != 0) {
        // The SDK only pin-muxes instance 0; other instances have no
        // wired-up SCL/SDA and would silently do nothing useful.
        mp_raise_msg_varg(&mp_type_NotImplementedError, MP_ERROR_TEXT("I2C(%d) is not wired up"), args[ARG_id].u_int);
    }
    if (args[ARG_scl].u_obj != MP_OBJ_NULL || args[ARG_sda].u_obj != MP_OBJ_NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("explicit choice of scl/sda is not implemented"));
    }

    machine_i2c_obj_t *self = &machine_i2c_obj[0];
    self->base.type = &machine_i2c_type;
    self->instance = 0;
    self->freq = args[ARG_freq].u_int;
    i2c_init(self->instance, self->freq);
    return MP_OBJ_FROM_PTR(self);
}

static const mp_machine_i2c_p_t machine_i2c_p = {
    #if MICROPY_PY_MACHINE_I2C_TRANSFER_WRITE1
    .transfer_supports_write1 = true,
    #endif
    .transfer = machine_i2c_transfer,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, machine_i2c_make_new,
    print, machine_i2c_print,
    protocol, &machine_i2c_p,
    locals_dict, &mp_machine_i2c_locals_dict
    );

#endif // MICROPY_PY_MACHINE_I2C
