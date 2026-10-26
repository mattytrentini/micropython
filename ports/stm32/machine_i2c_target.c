/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Damien P. George
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

// This file is never compiled standalone, it's included directly from
// extmod/machine_i2c_target.c via MICROPY_PY_MACHINE_I2C_TARGET_INCLUDEFILE.

#include "i2c.h"
#if defined(STM32C5)
#include "stm32c5xx_ll_bus.h"
#include "stm32c5xx_ll_i2c.h"
#endif
#if defined(STM32C5)
#define I2C1_ER_IRQn I2C1_ERR_IRQn
#if defined(I2C2_BASE)
#define I2C2_ER_IRQn I2C2_ERR_IRQn
#endif
#endif
#include "i2cslave.h"
#include "irq.h"

typedef struct _machine_i2c_target_obj_t {
    mp_obj_base_t base;
    I2C_TypeDef *i2c;
    uint16_t irqn_ev;
    uint16_t irqn_er;
    mp_hal_pin_obj_t scl;
    mp_hal_pin_obj_t sda;
} machine_i2c_target_obj_t;

static const machine_i2c_target_obj_t machine_i2c_target_obj[] = {
    #if defined(MICROPY_HW_I2C1_SCL)
    {{&machine_i2c_target_type}, I2C1, I2C1_EV_IRQn, I2C1_ER_IRQn, MICROPY_HW_I2C1_SCL, MICROPY_HW_I2C1_SDA},
    #else
    {{NULL}, NULL, 0, 0, NULL, NULL},
    #endif
    #if defined(MICROPY_HW_I2C2_SCL)
    {{&machine_i2c_target_type}, I2C2, I2C2_EV_IRQn, I2C2_ER_IRQn, MICROPY_HW_I2C2_SCL, MICROPY_HW_I2C2_SDA},
    #else
    {{NULL}, NULL, 0, 0, NULL, NULL},
    #endif
    #if defined(MICROPY_HW_I2C3_SCL)
    {{&machine_i2c_target_type}, I2C3, I2C3_EV_IRQn, I2C3_ER_IRQn, MICROPY_HW_I2C3_SCL, MICROPY_HW_I2C3_SDA},
    #else
    {{NULL}, NULL, 0, 0, NULL, NULL},
    #endif
    #if defined(MICROPY_HW_I2C4_SCL)
    {{&machine_i2c_target_type}, I2C4, I2C4_EV_IRQn, I2C4_ER_IRQn, MICROPY_HW_I2C4_SCL, MICROPY_HW_I2C4_SDA},
    #else
    {{NULL}, NULL, 0, 0, NULL, NULL},
    #endif
};

/******************************************************************************/
// stm32 hardware bindings

#if !defined(STM32C5)
static machine_i2c_target_obj_t *get_self(i2c_slave_t *i2c) {
    size_t i2c_id = ((uintptr_t)i2c - I2C1_BASE) / (I2C2_BASE - I2C1_BASE);
    return (machine_i2c_target_obj_t *)&machine_i2c_target_obj[i2c_id];
}

static machine_i2c_target_data_t *get_data(i2c_slave_t *i2c) {
    size_t i2c_id = ((uintptr_t)i2c - I2C1_BASE) / (I2C2_BASE - I2C1_BASE);
    return &machine_i2c_target_data[i2c_id];
}

int i2c_slave_process_addr_match(i2c_slave_t *i2c, int rw) {
    machine_i2c_target_data_addr_match(get_data(i2c), rw);
    return 0;
}

int i2c_slave_process_rx_byte(i2c_slave_t *i2c) {
    machine_i2c_target_data_write_request(get_self(i2c), get_data(i2c));
    return 0;
}

void i2c_slave_process_rx_end(i2c_slave_t *i2c) {
    machine_i2c_target_data_stop(get_data(i2c));
}

void i2c_slave_process_tx_byte(i2c_slave_t *i2c) {
    machine_i2c_target_data_read_request(get_self(i2c), get_data(i2c));
}

void i2c_slave_process_tx_end(i2c_slave_t *i2c) {
    machine_i2c_target_data_stop(get_data(i2c));
}
#else
void i2c_target_irq_handler(I2C_TypeDef *i2c) {
    size_t index = i2c == I2C1 ? 0 : 1;
    machine_i2c_target_obj_t *self = (machine_i2c_target_obj_t *)&machine_i2c_target_obj[index];
    machine_i2c_target_data_t *data = &machine_i2c_target_data[index];
    uint32_t isr = i2c->ISR;

    if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
        uint32_t errors = isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR);
        i2c->ICR = (errors & I2C_ISR_BERR ? I2C_ICR_BERRCF : 0)
            | (errors & I2C_ISR_ARLO ? I2C_ICR_ARLOCF : 0)
            | (errors & I2C_ISR_OVR ? I2C_ICR_OVRCF : 0);
        machine_i2c_target_data_stop(data);
        isr &= ~(I2C_ISR_RXNE | I2C_ISR_TXIS);
    }
    // Drain a pending byte from the previous transfer before a repeated START
    // changes the shared state machine to the next address direction.
    if (isr & I2C_ISR_RXNE) {
        machine_i2c_target_data_write_request(self, data);
    }
    if (isr & I2C_ISR_ADDR) {
        i2c->ICR = I2C_ICR_ADDRCF;
        machine_i2c_target_data_addr_match(data, (isr & I2C_ISR_DIR) != 0);
    }
    if (isr & I2C_ISR_TXIS) {
        machine_i2c_target_data_read_request(self, data);
    }
    if (isr & I2C_ISR_NACKF) {
        i2c->ICR = I2C_ICR_NACKCF;
    }
    if (isr & I2C_ISR_STOPF) {
        i2c->ICR = I2C_ICR_STOPCF;
        machine_i2c_target_data_stop(data);
    }
}
#endif

/******************************************************************************/
// I2CTarget port implementation

static inline size_t mp_machine_i2c_target_get_index(machine_i2c_target_obj_t *self) {
    return self - &machine_i2c_target_obj[0];
}

static inline void mp_machine_i2c_target_event_callback(machine_i2c_target_irq_obj_t *irq) {
    mp_irq_handler(&irq->base);
}

static size_t mp_machine_i2c_target_read_bytes(machine_i2c_target_obj_t *self, size_t len, uint8_t *buf) {
    if (len > 0) {
        #if defined(STM32C5)
        buf[0] = (uint8_t)self->i2c->RXDR;
        #else
        buf[0] = i2c_slave_read_byte(self->i2c);
        #endif
        len = 1;
    }
    return len;
}

static size_t mp_machine_i2c_target_write_bytes(machine_i2c_target_obj_t *self, size_t len, const uint8_t *buf) {
    if (len > 0) {
        #if defined(STM32C5)
        self->i2c->TXDR = buf[0];
        #else
        i2c_slave_write_byte(self->i2c, buf[0]);
        #endif
        len = 1;
    }
    return len;
}

static inline void mp_machine_i2c_target_irq_config(machine_i2c_target_obj_t *self, unsigned int trigger) {
    (void)self;
    (void)trigger;
}

static mp_obj_t mp_machine_i2c_target_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    // Parse arguments.
    enum { ARG_id, ARG_addr, ARG_addrsize, ARG_mem, ARG_mem_addrsize };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_addr, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_addrsize, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 7} },
        { MP_QSTR_mem, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_mem_addrsize, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    #if defined(STM32C5)
    mp_int_t addr_size_val = args[ARG_addrsize].u_int;
    mp_int_t addr_val = args[ARG_addr].u_int;
    if (addr_size_val != 7 && addr_size_val != 10) {
        mp_raise_ValueError(MP_ERROR_TEXT("addrsize must be 7 or 10"));
    }
    if (addr_val < 0 || addr_val >= (1 << addr_size_val)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid address"));
    }
    #endif
    // Work out I2C bus.
    int i2c_id = i2c_find_peripheral(args[ARG_id].u_obj);

    // Get static target object.
    machine_i2c_target_obj_t *self = (machine_i2c_target_obj_t *)&machine_i2c_target_obj[i2c_id - 1];

    // Initialise data.
    MP_STATE_PORT(machine_i2c_target_mem_obj)[i2c_id - 1] = args[ARG_mem].u_obj;
    machine_i2c_target_data_t *data = &machine_i2c_target_data[i2c_id - 1];
    machine_i2c_target_data_init(data, args[ARG_mem].u_obj, args[ARG_mem_addrsize].u_int);
    #if defined(STM32C5)
    if (args[ARG_mem].u_obj != mp_const_none && data->mem_len == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem buffer cannot be empty"));
    }
    #endif

    #if defined(STM32C5)
    int ret = i2c_init(self->i2c, self->scl, self->sda, 100000, 1000);
    if (ret < 0) {
        mp_raise_OSError(-ret);
    }
    uint32_t target_addr_size = args[ARG_addrsize].u_int;
    uint32_t target_addr = args[ARG_addr].u_int;
    self->i2c->OAR1 = I2C_OAR1_OA1EN | (target_addr_size == 10 ? I2C_OAR1_OA1MODE | target_addr : target_addr << 1);
    self->i2c->ICR = I2C_ICR_ADDRCF | I2C_ICR_NACKCF | I2C_ICR_STOPCF
        | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
    NVIC_ClearPendingIRQ(self->irqn_ev);
    NVIC_ClearPendingIRQ(self->irqn_er);
    i2c_target_enabled |= 1 << (i2c_id - 1);
    self->i2c->CR1 = I2C_CR1_PE | I2C_CR1_TXIE | I2C_CR1_RXIE | I2C_CR1_ADDRIE
        | I2C_CR1_NACKIE | I2C_CR1_STOPIE | I2C_CR1_ERRIE;
    NVIC_SetPriority(self->irqn_ev, IRQ_PRI_I2C);
    NVIC_SetPriority(self->irqn_er, IRQ_PRI_I2C);
    NVIC_EnableIRQ(self->irqn_ev);
    NVIC_EnableIRQ(self->irqn_er);
    #else
    mp_hal_pin_config_alt(self->scl, MP_HAL_PIN_MODE_ALT_OPEN_DRAIN, MP_HAL_PIN_PULL_NONE, AF_FN_I2C, i2c_id);
    mp_hal_pin_config_alt(self->sda, MP_HAL_PIN_MODE_ALT_OPEN_DRAIN, MP_HAL_PIN_PULL_NONE, AF_FN_I2C, i2c_id);
    i2c_slave_init(self->i2c, self->irqn_ev, IRQ_PRI_I2C, args[ARG_addr].u_int);
    NVIC_SetPriority(self->irqn_er, IRQ_PRI_I2C);
    NVIC_EnableIRQ(self->irqn_er);
    #endif

    i2c_target_enabled |= 1 << (i2c_id - 1);

    return MP_OBJ_FROM_PTR(self);
}

static void mp_machine_i2c_target_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_i2c_target_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t oar1 = self->i2c->OAR1;
    #if defined(STM32C5)
    uint32_t addr = oar1 & I2C_OAR1_OA1;
    if (!(oar1 & I2C_OAR1_OA1MODE)) {
        addr >>= 1;
    }
    #else
    uint32_t addr = (oar1 >> 1) & 0x7f;
    #endif
    mp_printf(print, "I2CTarget(%u, addr=%u)", self - &machine_i2c_target_obj[0] + 1, addr);
}

static void mp_machine_i2c_target_deinit(machine_i2c_target_obj_t *self) {
    #if defined(STM32C5)
    self->i2c->CR1 = 0;
    i2c_target_enabled &= ~(1 << mp_machine_i2c_target_get_index(self));
    NVIC_DisableIRQ(self->irqn_ev);
    NVIC_DisableIRQ(self->irqn_er);
    NVIC_ClearPendingIRQ(self->irqn_ev);
    NVIC_ClearPendingIRQ(self->irqn_er);
    i2c_deinit_controller(self->i2c);
    #else
    i2c_slave_shutdown(self->i2c, self->irqn_ev);
    NVIC_DisableIRQ(self->irqn_er);
    #endif
}
