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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mphal.h"
#include "extmod/modmachine.h"

#include "driver/spi_master.h"
#include "soc/gpio_sig_map.h"
#include "soc/spi_pins.h"

// QuadSPI mappings by device, same underlying hosts as machine.SPI.
// MicroPython    | ESP32     | ESP32S2   | ESP32S3 | ESP32C3 | ESP32C6
// ---------------+-----------+-----------+---------+---------+---------
// QuadSPI(id=1)  | HSPI/SPI2 | FSPI/SPI2 | SPI2    | SPI2    | SPI2
// QuadSPI(id=2)  | VSPI/SPI3 | HSPI/SPI3 | SPI3    | err     | err
//
// Unlike machine.SPI, there are no universally-safe default quad pins: the
// IOMUX quad-capable lines on a host are frequently the same lines used by
// the SoC's own flash/PSRAM. A board may opt in to defaults by defining
// MICROPY_HW_QUADSPIn_SCK/IO0/IO1/IO2/IO3; otherwise all pins are required
// constructor/init arguments.

#if SOC_SPI_PERIPH_NUM > 2
#define MICROPY_HW_QUADSPI_MAX (2)
#else
#define MICROPY_HW_QUADSPI_MAX (1)
#endif

#ifndef MICROPY_HW_QUADSPI1_SCK
#define MICROPY_HW_QUADSPI1_SCK (-2)
#define MICROPY_HW_QUADSPI1_IO0 (-2)
#define MICROPY_HW_QUADSPI1_IO1 (-2)
#define MICROPY_HW_QUADSPI1_IO2 (-2)
#define MICROPY_HW_QUADSPI1_IO3 (-2)
#endif

#ifndef MICROPY_HW_QUADSPI2_SCK
#define MICROPY_HW_QUADSPI2_SCK (-2)
#define MICROPY_HW_QUADSPI2_IO0 (-2)
#define MICROPY_HW_QUADSPI2_IO1 (-2)
#define MICROPY_HW_QUADSPI2_IO2 (-2)
#define MICROPY_HW_QUADSPI2_IO3 (-2)
#endif

#define MP_HW_QUADSPI_MAX_XFER_BYTES (4092)
#define MP_HW_QUADSPI_MAX_XFER_BITS (MP_HW_QUADSPI_MAX_XFER_BYTES * 8) // Has to be an even multiple of 8

typedef struct _machine_hw_quadspi_default_pins_t {
    union {
        int8_t array[5];
        struct {
            // Must be in enum's ARG_sck, ARG_io0, ..., ARG_io3 order
            int8_t sck;
            int8_t io0;
            int8_t io1;
            int8_t io2;
            int8_t io3;
        } pins;
    };
} machine_hw_quadspi_default_pins_t;

typedef struct _machine_hw_quadspi_obj_t {
    mp_obj_base_t base;
    spi_host_device_t host;
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    uint8_t firstbit;
    int8_t sck;
    int8_t io[4];
    spi_device_handle_t spi;
    enum {
        MACHINE_HW_QUADSPI_STATE_NONE,
        MACHINE_HW_QUADSPI_STATE_INIT,
        MACHINE_HW_QUADSPI_STATE_DEINIT
    } state;
} machine_hw_quadspi_obj_t;

// Default pin mappings for the hardware QuadSPI instances (see comment above:
// -2 means "no default", ie the pin must be given explicitly).
static const machine_hw_quadspi_default_pins_t machine_hw_quadspi_default_pins[MICROPY_HW_QUADSPI_MAX] = {
    { .pins = { .sck = MICROPY_HW_QUADSPI1_SCK, .io0 = MICROPY_HW_QUADSPI1_IO0, .io1 = MICROPY_HW_QUADSPI1_IO1, .io2 = MICROPY_HW_QUADSPI1_IO2, .io3 = MICROPY_HW_QUADSPI1_IO3 }},
    #if MICROPY_HW_QUADSPI_MAX > 1
    { .pins = { .sck = MICROPY_HW_QUADSPI2_SCK, .io0 = MICROPY_HW_QUADSPI2_IO0, .io1 = MICROPY_HW_QUADSPI2_IO1, .io2 = MICROPY_HW_QUADSPI2_IO2, .io3 = MICROPY_HW_QUADSPI2_IO3 }},
    #endif
};

// Common arguments for init() and make_new()
enum { ARG_id, ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit, ARG_sck, ARG_io0, ARG_io1, ARG_io2, ARG_io3 };
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

// Static objects mapping to SPI2 (and SPI3 if available) hardware peripherals,
// shared address space with machine.SPI's peripheral numbering (but a separate
// set of objects/state -- the underlying ESP-IDF host can't be used by both
// machine.SPI and machine.QuadSPI at the same time; attempting to do so fails
// with "SPI host already in use" from spi_bus_initialize()).
static machine_hw_quadspi_obj_t machine_hw_quadspi_obj[MICROPY_HW_QUADSPI_MAX];

static void machine_hw_quadspi_deinit_internal(machine_hw_quadspi_obj_t *self) {
    switch (spi_bus_remove_device(self->spi)) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            return;

        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SPI device already freed"));
            return;
    }

    switch (spi_bus_free(self->host)) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            return;

        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SPI bus already freed"));
            return;
    }

    int8_t pins[5] = {self->sck, self->io[0], self->io[1], self->io[2], self->io[3]};

    for (int i = 0; i < 5; i++) {
        if (pins[i] != -1) {
            esp_rom_gpio_pad_select_gpio(pins[i]);
            esp_rom_gpio_connect_out_signal(pins[i], SIG_GPIO_OUT_IDX, false, false);
            gpio_set_direction(pins[i], GPIO_MODE_INPUT);
        }
    }
}

static void machine_hw_quadspi_init_internal(machine_hw_quadspi_obj_t *self, mp_arg_val_t args[]) {

    // if we're not initialized, then we're
    // implicitly 'changed', since this is the init routine
    bool changed = self->state != MACHINE_HW_QUADSPI_STATE_INIT;

    esp_err_t ret;

    machine_hw_quadspi_obj_t old_self = *self;

    if (args[ARG_baudrate].u_int != -1) {
        // calculate the actual clock frequency that the SPI peripheral can produce
        uint32_t baudrate = spi_get_actual_clock(APB_CLK_FREQ, args[ARG_baudrate].u_int, 0);
        if (baudrate != self->baudrate) {
            self->baudrate = baudrate;
            changed = true;
        }
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

    if (args[ARG_sck].u_int != -2 && args[ARG_sck].u_int != self->sck) {
        self->sck = args[ARG_sck].u_int;
        changed = true;
    }

    for (int i = 0; i < 4; i++) {
        mp_int_t pin = args[ARG_io0 + i].u_int;
        if (pin != -2 && pin != self->io[i]) {
            self->io[i] = pin;
            changed = true;
        }
    }

    if (changed) {
        if (self->state == MACHINE_HW_QUADSPI_STATE_INIT) {
            self->state = MACHINE_HW_QUADSPI_STATE_DEINIT;
            machine_hw_quadspi_deinit_internal(&old_self);
        }
    } else {
        return; // no changes
    }

    spi_bus_config_t buscfg = {
        .data0_io_num = self->io[0],
        .data1_io_num = self->io[1],
        .data2_io_num = self->io[2],
        .data3_io_num = self->io[3],
        .sclk_io_num = self->sck,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = self->baudrate,
        .mode = self->phase | (self->polarity << 1),
        .spics_io_num = -1, // No CS pin
        .queue_size = 2,
        // Quad data lines are half-duplex (shared/bidirectional); read and
        // write are always separate transactions.
        .flags = SPI_DEVICE_HALFDUPLEX,
        .pre_cb = NULL
    };

    // Initialize the SPI bus

    // Select DMA channel based on the hardware SPI host
    int dma_chan = 0;
    #if CONFIG_IDF_TARGET_ESP32
    if (self->host == SPI2_HOST) {
        dma_chan = 1;
    } else {
        dma_chan = 2;
    }
    #else
    dma_chan = SPI_DMA_CH_AUTO;
    #endif

    ret = spi_bus_initialize(self->host, &buscfg, dma_chan);
    switch (ret) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            return;

        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SPI host already in use"));
            return;
    }

    ret = spi_bus_add_device(self->host, &devcfg, &self->spi);
    switch (ret) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            spi_bus_free(self->host);
            return;

        case ESP_ERR_NO_MEM:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("out of memory"));
            spi_bus_free(self->host);
            return;

        case ESP_ERR_NOT_FOUND:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no free slots"));
            spi_bus_free(self->host);
            return;
    }
    self->state = MACHINE_HW_QUADSPI_STATE_INIT;
}

static void machine_hw_quadspi_deinit(mp_obj_base_t *self_in) {
    machine_hw_quadspi_obj_t *self = (machine_hw_quadspi_obj_t *)self_in;
    if (self->state == MACHINE_HW_QUADSPI_STATE_INIT) {
        self->state = MACHINE_HW_QUADSPI_STATE_DEINIT;
        machine_hw_quadspi_deinit_internal(self);
    }
}

static mp_uint_t machine_hw_spi_multi_gcd(mp_uint_t x, mp_uint_t y) {
    while (x != y) {
        if (x > y) {
            x -= y;
        } else {
            y -= x;
        }
    }
    return x;
}

// Queue up to MP_HW_QUADSPI_MAX_XFER_BITS-sized chunks of a single-direction
// (all-write or all-read) transfer, DMA-driven via the same
// acquire/queue/get_trans_result pattern machine.SPI uses. Shared by
// machine.QuadSPI and machine.OctoSPI -- the line count only affects bus/device
// config, not this transaction-queueing logic.
static void machine_hw_spi_multi_transfer_one_way(spi_device_handle_t spi, uint8_t bits, size_t len, const uint8_t *src, uint8_t *dest) {
    // Round to nearest whole set of bits
    int bits_to_send = len * 8 / bits * bits;

    if (!bits_to_send) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too short"));
    }

    if (len <= 4 && src != NULL) {
        spi_transaction_t transaction = { 0 };
        memcpy(&transaction.tx_data, src, len);
        transaction.flags = SPI_TRANS_USE_TXDATA;
        transaction.length = bits_to_send;
        spi_device_transmit(spi, &transaction);
        return;
    }

    if (len <= 4 && dest != NULL) {
        spi_transaction_t transaction = { 0 };
        transaction.flags = SPI_TRANS_USE_RXDATA;
        // Half-duplex: .length is the (absent) tx phase, .rxlength is the rx phase.
        transaction.length = 0;
        transaction.rxlength = bits_to_send;
        spi_device_transmit(spi, &transaction);
        memcpy(dest, &transaction.rx_data, len);
        return;
    }

    int offset = 0;
    int bits_remaining = bits_to_send;
    int optimum_word_size = 8 * bits / machine_hw_spi_multi_gcd(8, bits);
    int max_transaction_bits = MP_HW_QUADSPI_MAX_XFER_BITS / optimum_word_size * optimum_word_size;
    spi_transaction_t *transaction, *result, transactions[2];
    int i = 0;

    spi_device_acquire_bus(spi, portMAX_DELAY);

    while (bits_remaining) {
        transaction = transactions + i++ % 2;
        memset(transaction, 0, sizeof(spi_transaction_t));

        int chunk_bits = bits_remaining > max_transaction_bits ? max_transaction_bits : bits_remaining;

        // Half-duplex: .length is the tx phase, .rxlength is the rx phase --
        // exactly one of src/dest is non-NULL, so only one phase is active.
        if (src != NULL) {
            transaction->length = chunk_bits;
            transaction->tx_buffer = src + offset;
        } else {
            transaction->rxlength = chunk_bits;
            transaction->rx_buffer = dest + offset;
        }

        spi_device_queue_trans(spi, transaction, portMAX_DELAY);
        bits_remaining -= chunk_bits;

        if (offset > 0) {
            // wait for previously queued transaction
            MP_THREAD_GIL_EXIT();
            spi_device_get_trans_result(spi, &result, portMAX_DELAY);
            MP_THREAD_GIL_ENTER();
        }

        // doesn't need ceil(); loop ends when bits_remaining is 0
        offset += chunk_bits / 8;
    }

    // wait for last transaction
    MP_THREAD_GIL_EXIT();
    spi_device_get_trans_result(spi, &result, portMAX_DELAY);
    MP_THREAD_GIL_ENTER();
    spi_device_release_bus(spi);
}

static void machine_hw_quadspi_write(mp_obj_base_t *self_in, size_t len, const uint8_t *src) {
    machine_hw_quadspi_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->state == MACHINE_HW_QUADSPI_STATE_DEINIT) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("write on deinitialized QuadSPI"));
        return;
    }

    machine_hw_spi_multi_transfer_one_way(self->spi, self->bits, len, src, NULL);
}

static void machine_hw_quadspi_read(mp_obj_base_t *self_in, size_t len, uint8_t *dest) {
    machine_hw_quadspi_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->state == MACHINE_HW_QUADSPI_STATE_DEINIT) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("read on deinitialized QuadSPI"));
        return;
    }

    machine_hw_spi_multi_transfer_one_way(self->spi, self->bits, len, NULL, dest);
}

/******************************************************************************/
// MicroPython bindings for hw_quadspi

static void machine_hw_quadspi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_hw_quadspi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "QuadSPI(id=%u, baudrate=%u, polarity=%u, phase=%u, bits=%u, firstbit=%u,"
        " sck=%d, io0=%d, io1=%d, io2=%d, io3=%d)",
        self->host, self->baudrate, self->polarity,
        self->phase, self->bits, self->firstbit,
        self->sck, self->io[0], self->io[1], self->io[2], self->io[3]);
}

// Take an arg list made from quadspi_allowed_args, and put in default or "keep same" values
// into all the u_int fields.
// The behavior is slightly different for a new call vs an init method on an existing object.
// Unspecified arguments for new will use defaults, for init they keep the existing value.
static void machine_hw_quadspi_argcheck(mp_arg_val_t args[], const machine_hw_quadspi_default_pins_t *default_pins) {
    // A non-NULL default_pins argument will trigger the "use default" behavior.
    // Replace pin args with default/current values for new vs init call, respectively
    for (int i = ARG_sck; i <= ARG_io3; i++) {
        if (args[i].u_obj == MP_OBJ_NULL) {
            args[i].u_int = default_pins ? default_pins->array[i - ARG_sck] : -2;
        } else if (args[i].u_obj == mp_const_none) {
            args[i].u_int = -1;
        } else {
            args[i].u_int = machine_pin_get_id(args[i].u_obj);
        }
    }
}

static void machine_hw_quadspi_init(mp_obj_base_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_hw_quadspi_obj_t *self = (machine_hw_quadspi_obj_t *)self_in;

    mp_arg_val_t args[MP_ARRAY_SIZE(quadspi_allowed_args)];
    // offset arg lists by 1 to skip first arg, id, which is not valid for init()
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(quadspi_allowed_args) - 1,
        quadspi_allowed_args + 1, args + 1);

    machine_hw_quadspi_argcheck(args, NULL);
    machine_hw_quadspi_init_internal(self, args);
}

static mp_obj_t machine_hw_quadspi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(quadspi_allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(quadspi_allowed_args), quadspi_allowed_args, args);

    const mp_int_t quadspi_id = args[ARG_id].u_int;
    if (1 <= quadspi_id && quadspi_id <= MICROPY_HW_QUADSPI_MAX) {
        machine_hw_quadspi_argcheck(args, &machine_hw_quadspi_default_pins[quadspi_id - 1]);
    } else {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("QuadSPI(%d) doesn't exist"), quadspi_id);
    }

    for (int i = ARG_sck; i <= ARG_io3; i++) {
        if (args[i].u_int < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("must specify sck/io0/io1/io2/io3"));
        }
    }

    // Replace -1 non-pin args with default values
    static const mp_int_t defaults[] = { 500000, 0, 0, 8, MICROPY_PY_MACHINE_SPI_MSB };
    for (int i = ARG_baudrate; i <= ARG_firstbit; i++) {
        if (args[i].u_int == -1) {
            args[i].u_int = defaults[i - ARG_baudrate];
        }
    }

    machine_hw_quadspi_obj_t *self = &machine_hw_quadspi_obj[quadspi_id - 1];
    self->host = quadspi_id;

    self->base.type = &machine_quadspi_type;

    machine_hw_quadspi_init_internal(self, args);

    return MP_OBJ_FROM_PTR(self);
}

static const mp_machine_quadspi_p_t machine_hw_quadspi_p = {
    .init = machine_hw_quadspi_init,
    .deinit = machine_hw_quadspi_deinit,
    .write = machine_hw_quadspi_write,
    .read = machine_hw_quadspi_read,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_quadspi_type,
    MP_QSTR_QuadSPI,
    MP_TYPE_FLAG_NONE,
    make_new, machine_hw_quadspi_make_new,
    print, machine_hw_quadspi_print,
    protocol, &machine_hw_quadspi_p,
    locals_dict, &mp_machine_quadspi_locals_dict
    );

/******************************************************************************/
// MicroPython bindings for machine.OctoSPI
//
// Only SPI2_HOST supports octal mode (SPICOMMON_BUSFLAG_OCTAL), so unlike
// QuadSPI there's only ever a single OctoSPI(1). This whole section only
// compiles when MICROPY_PY_MACHINE_OCTOSPI (SOC_SPI_SUPPORT_OCT) is set, ie
// on chips that actually have octal-capable SPI hardware (eg ESP32-S3/P4).

#if MICROPY_PY_MACHINE_OCTOSPI

#define MICROPY_HW_OCTOSPI_MAX (1)

#ifndef MICROPY_HW_OCTOSPI1_SCK
#define MICROPY_HW_OCTOSPI1_SCK (-2)
#define MICROPY_HW_OCTOSPI1_IO0 (-2)
#define MICROPY_HW_OCTOSPI1_IO1 (-2)
#define MICROPY_HW_OCTOSPI1_IO2 (-2)
#define MICROPY_HW_OCTOSPI1_IO3 (-2)
#define MICROPY_HW_OCTOSPI1_IO4 (-2)
#define MICROPY_HW_OCTOSPI1_IO5 (-2)
#define MICROPY_HW_OCTOSPI1_IO6 (-2)
#define MICROPY_HW_OCTOSPI1_IO7 (-2)
#endif

typedef struct _machine_hw_octospi_default_pins_t {
    union {
        int8_t array[9];
        struct {
            // Must be in enum's ARG_o_sck, ARG_o_io0, ..., ARG_o_io7 order
            int8_t sck;
            int8_t io0;
            int8_t io1;
            int8_t io2;
            int8_t io3;
            int8_t io4;
            int8_t io5;
            int8_t io6;
            int8_t io7;
        } pins;
    };
} machine_hw_octospi_default_pins_t;

typedef struct _machine_hw_octospi_obj_t {
    mp_obj_base_t base;
    spi_host_device_t host;
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    uint8_t firstbit;
    int8_t sck;
    int8_t io[8];
    spi_device_handle_t spi;
    enum {
        MACHINE_HW_OCTOSPI_STATE_NONE,
        MACHINE_HW_OCTOSPI_STATE_INIT,
        MACHINE_HW_OCTOSPI_STATE_DEINIT
    } state;
} machine_hw_octospi_obj_t;

static const machine_hw_octospi_default_pins_t machine_hw_octospi_default_pins[MICROPY_HW_OCTOSPI_MAX] = {
    { .pins = {
        .sck = MICROPY_HW_OCTOSPI1_SCK,
        .io0 = MICROPY_HW_OCTOSPI1_IO0, .io1 = MICROPY_HW_OCTOSPI1_IO1,
        .io2 = MICROPY_HW_OCTOSPI1_IO2, .io3 = MICROPY_HW_OCTOSPI1_IO3,
        .io4 = MICROPY_HW_OCTOSPI1_IO4, .io5 = MICROPY_HW_OCTOSPI1_IO5,
        .io6 = MICROPY_HW_OCTOSPI1_IO6, .io7 = MICROPY_HW_OCTOSPI1_IO7,
    }},
};

enum {
    ARG_o_id, ARG_o_baudrate, ARG_o_polarity, ARG_o_phase, ARG_o_bits, ARG_o_firstbit,
    ARG_o_sck, ARG_o_io0, ARG_o_io1, ARG_o_io2, ARG_o_io3, ARG_o_io4, ARG_o_io5, ARG_o_io6, ARG_o_io7
};
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

static machine_hw_octospi_obj_t machine_hw_octospi_obj[MICROPY_HW_OCTOSPI_MAX];

static void machine_hw_octospi_deinit_internal(machine_hw_octospi_obj_t *self) {
    switch (spi_bus_remove_device(self->spi)) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            return;

        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SPI device already freed"));
            return;
    }

    switch (spi_bus_free(self->host)) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            return;

        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SPI bus already freed"));
            return;
    }

    int8_t pins[9] = {
        self->sck,
        self->io[0], self->io[1], self->io[2], self->io[3],
        self->io[4], self->io[5], self->io[6], self->io[7],
    };

    for (int i = 0; i < 9; i++) {
        if (pins[i] != -1) {
            esp_rom_gpio_pad_select_gpio(pins[i]);
            esp_rom_gpio_connect_out_signal(pins[i], SIG_GPIO_OUT_IDX, false, false);
            gpio_set_direction(pins[i], GPIO_MODE_INPUT);
        }
    }
}

static void machine_hw_octospi_init_internal(machine_hw_octospi_obj_t *self, mp_arg_val_t args[]) {

    bool changed = self->state != MACHINE_HW_OCTOSPI_STATE_INIT;

    esp_err_t ret;

    machine_hw_octospi_obj_t old_self = *self;

    if (args[ARG_o_baudrate].u_int != -1) {
        uint32_t baudrate = spi_get_actual_clock(APB_CLK_FREQ, args[ARG_o_baudrate].u_int, 0);
        if (baudrate != self->baudrate) {
            self->baudrate = baudrate;
            changed = true;
        }
    }

    if (args[ARG_o_polarity].u_int != -1 && args[ARG_o_polarity].u_int != self->polarity) {
        self->polarity = args[ARG_o_polarity].u_int;
        changed = true;
    }

    if (args[ARG_o_phase].u_int != -1 && args[ARG_o_phase].u_int != self->phase) {
        self->phase = args[ARG_o_phase].u_int;
        changed = true;
    }

    if (args[ARG_o_bits].u_int != -1 && args[ARG_o_bits].u_int <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid bits"));
    }

    if (args[ARG_o_bits].u_int != -1 && args[ARG_o_bits].u_int != self->bits) {
        self->bits = args[ARG_o_bits].u_int;
        changed = true;
    }

    if (args[ARG_o_firstbit].u_int != -1) {
        if (args[ARG_o_firstbit].u_int != MICROPY_PY_MACHINE_SPI_MSB) {
            mp_raise_ValueError(MP_ERROR_TEXT("firstbit must be MSB"));
        }
        if (args[ARG_o_firstbit].u_int != self->firstbit) {
            self->firstbit = args[ARG_o_firstbit].u_int;
            changed = true;
        }
    }

    if (args[ARG_o_sck].u_int != -2 && args[ARG_o_sck].u_int != self->sck) {
        self->sck = args[ARG_o_sck].u_int;
        changed = true;
    }

    for (int i = 0; i < 8; i++) {
        mp_int_t pin = args[ARG_o_io0 + i].u_int;
        if (pin != -2 && pin != self->io[i]) {
            self->io[i] = pin;
            changed = true;
        }
    }

    if (changed) {
        if (self->state == MACHINE_HW_OCTOSPI_STATE_INIT) {
            self->state = MACHINE_HW_OCTOSPI_STATE_DEINIT;
            machine_hw_octospi_deinit_internal(&old_self);
        }
    } else {
        return; // no changes
    }

    spi_bus_config_t buscfg = {
        .data0_io_num = self->io[0],
        .data1_io_num = self->io[1],
        .data2_io_num = self->io[2],
        .data3_io_num = self->io[3],
        .data4_io_num = self->io[4],
        .data5_io_num = self->io[5],
        .data6_io_num = self->io[6],
        .data7_io_num = self->io[7],
        .sclk_io_num = self->sck,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_OCTAL,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = self->baudrate,
        .mode = self->phase | (self->polarity << 1),
        .spics_io_num = -1, // No CS pin
        .queue_size = 2,
        // Octal data lines are half-duplex (shared/bidirectional); read and
        // write are always separate transactions.
        .flags = SPI_DEVICE_HALFDUPLEX,
        .pre_cb = NULL
    };

    // Octal mode is only available on targets with SOC_SPI_SUPPORT_OCT, which
    // always use the automatic DMA channel (no classic-ESP32 special case).
    ret = spi_bus_initialize(self->host, &buscfg, SPI_DMA_CH_AUTO);
    switch (ret) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            return;

        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SPI host already in use"));
            return;
    }

    ret = spi_bus_add_device(self->host, &devcfg, &self->spi);
    switch (ret) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            spi_bus_free(self->host);
            return;

        case ESP_ERR_NO_MEM:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("out of memory"));
            spi_bus_free(self->host);
            return;

        case ESP_ERR_NOT_FOUND:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no free slots"));
            spi_bus_free(self->host);
            return;
    }
    self->state = MACHINE_HW_OCTOSPI_STATE_INIT;
}

static void machine_hw_octospi_deinit(mp_obj_base_t *self_in) {
    machine_hw_octospi_obj_t *self = (machine_hw_octospi_obj_t *)self_in;
    if (self->state == MACHINE_HW_OCTOSPI_STATE_INIT) {
        self->state = MACHINE_HW_OCTOSPI_STATE_DEINIT;
        machine_hw_octospi_deinit_internal(self);
    }
}

static void machine_hw_octospi_write(mp_obj_base_t *self_in, size_t len, const uint8_t *src) {
    machine_hw_octospi_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->state == MACHINE_HW_OCTOSPI_STATE_DEINIT) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("write on deinitialized OctoSPI"));
        return;
    }

    machine_hw_spi_multi_transfer_one_way(self->spi, self->bits, len, src, NULL);
}

static void machine_hw_octospi_read(mp_obj_base_t *self_in, size_t len, uint8_t *dest) {
    machine_hw_octospi_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->state == MACHINE_HW_OCTOSPI_STATE_DEINIT) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("read on deinitialized OctoSPI"));
        return;
    }

    machine_hw_spi_multi_transfer_one_way(self->spi, self->bits, len, NULL, dest);
}

static void machine_hw_octospi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_hw_octospi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "OctoSPI(id=%u, baudrate=%u, polarity=%u, phase=%u, bits=%u, firstbit=%u,"
        " sck=%d, io0=%d, io1=%d, io2=%d, io3=%d, io4=%d, io5=%d, io6=%d, io7=%d)",
        self->host, self->baudrate, self->polarity,
        self->phase, self->bits, self->firstbit,
        self->sck, self->io[0], self->io[1], self->io[2], self->io[3],
        self->io[4], self->io[5], self->io[6], self->io[7]);
}

static void machine_hw_octospi_argcheck(mp_arg_val_t args[], const machine_hw_octospi_default_pins_t *default_pins) {
    for (int i = ARG_o_sck; i <= ARG_o_io7; i++) {
        if (args[i].u_obj == MP_OBJ_NULL) {
            args[i].u_int = default_pins ? default_pins->array[i - ARG_o_sck] : -2;
        } else if (args[i].u_obj == mp_const_none) {
            args[i].u_int = -1;
        } else {
            args[i].u_int = machine_pin_get_id(args[i].u_obj);
        }
    }
}

static void machine_hw_octospi_init(mp_obj_base_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_hw_octospi_obj_t *self = (machine_hw_octospi_obj_t *)self_in;

    mp_arg_val_t args[MP_ARRAY_SIZE(octospi_allowed_args)];
    // offset arg lists by 1 to skip first arg, id, which is not valid for init()
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(octospi_allowed_args) - 1,
        octospi_allowed_args + 1, args + 1);

    machine_hw_octospi_argcheck(args, NULL);
    machine_hw_octospi_init_internal(self, args);
}

static mp_obj_t machine_hw_octospi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(octospi_allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(octospi_allowed_args), octospi_allowed_args, args);

    const mp_int_t octospi_id = args[ARG_o_id].u_int;
    if (1 <= octospi_id && octospi_id <= MICROPY_HW_OCTOSPI_MAX) {
        machine_hw_octospi_argcheck(args, &machine_hw_octospi_default_pins[octospi_id - 1]);
    } else {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("OctoSPI(%d) doesn't exist"), octospi_id);
    }

    for (int i = ARG_o_sck; i <= ARG_o_io7; i++) {
        if (args[i].u_int < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("must specify sck/io0/../io7"));
        }
    }

    // Replace -1 non-pin args with default values
    static const mp_int_t defaults[] = { 500000, 0, 0, 8, MICROPY_PY_MACHINE_SPI_MSB };
    for (int i = ARG_o_baudrate; i <= ARG_o_firstbit; i++) {
        if (args[i].u_int == -1) {
            args[i].u_int = defaults[i - ARG_o_baudrate];
        }
    }

    machine_hw_octospi_obj_t *self = &machine_hw_octospi_obj[octospi_id - 1];
    self->host = octospi_id; // id=1 -> SPI2_HOST, the only octal-capable host

    self->base.type = &machine_octospi_type;

    machine_hw_octospi_init_internal(self, args);

    return MP_OBJ_FROM_PTR(self);
}

static const mp_machine_quadspi_p_t machine_hw_octospi_p = {
    .init = machine_hw_octospi_init,
    .deinit = machine_hw_octospi_deinit,
    .write = machine_hw_octospi_write,
    .read = machine_hw_octospi_read,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_octospi_type,
    MP_QSTR_OctoSPI,
    MP_TYPE_FLAG_NONE,
    make_new, machine_hw_octospi_make_new,
    print, machine_hw_octospi_print,
    protocol, &machine_hw_octospi_p,
    locals_dict, &mp_machine_quadspi_locals_dict
    );

#endif // MICROPY_PY_MACHINE_OCTOSPI
