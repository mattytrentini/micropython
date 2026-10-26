/*
 * This file is part of the MicroPython project, http://micropython.org/
 * SPDX-License-Identifier: MIT
 */

// STM32C5 uses the CubeMX2 FDCAN HAL, not the legacy HAL API used by
// ports/stm32/machine_can.c. This backend deliberately configures only
// Classical CAN frames despite using the FDCAN controller.
#include <stdbool.h>
#include <string.h>
#include "extmod/machine_can_port.h"
#include "stm32c5xx_hal_fdcan.h"
#include "stm32c5xx_hal_rcc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/gc.h"
#include "irq.h"

#define CAN_BRP_MIN (1)
#define CAN_BRP_MAX (255)
#define CAN_TSEG1_MAX (16)
#define CAN_TSEG2_MAX (8)
#define CAN_SJW_MAX (4)
#define CAN_FILTERS_STD_EXT_SEPARATE (1)
#define CAN_TX_QUEUE_LEN (3)
#define CAN_C5_STD_FILTERS (28)
#define CAN_C5_EXT_FILTERS (8)
#define CAN_HW_MAX_FILTER (CAN_C5_STD_FILTERS + CAN_C5_EXT_FILTERS)
#define CAN_C5_TX_EMPTY UINT32_MAX

struct machine_can_port {
    hal_fdcan_handle_t h;
    FDCAN_GlobalTypeDef *regs;
    uint32_t tx_id[CAN_TX_QUEUE_LEN];
    uint8_t tx_inflight;
    uint8_t tx_done;
    uint8_t tx_failed;
    bool irq_state_pending;
    bool error_passive;
};

static machine_can_obj_t *machine_can_c5_obj;
static bool machine_can_c5_clock_set;

static hal_fdcan_t machine_can_c5_instance(void) {
    #if defined(MICROPY_HW_CAN1_FDCAN_ID) && MICROPY_HW_CAN1_FDCAN_ID == 2
    return HAL_FDCAN2;
    #else
    return HAL_FDCAN1;
    #endif
}

static int machine_can_port_f_clock(const machine_can_obj_t *self) {
    (void)self;
    // Both supported C5 boards run a 48 MHz HSE. Selecting it directly avoids
    // deriving CAN timing from the mutable SYSCLK/APB prescalers.
    if (!machine_can_c5_clock_set) {
        if (HAL_RCC_FDCAN_SetKernelClkSource(HAL_RCC_FDCAN_CLK_SRC_HSE) != HAL_OK) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("FDCAN clock selection failed"));
        }
        machine_can_c5_clock_set = true;
    }
    return (int)HAL_RCC_FDCAN_GetKernelClkFreq();
}

static bool machine_can_port_supports_mode(const machine_can_obj_t *self, machine_can_mode_t mode) {
    (void)self;
    return mode <= MP_CAN_MODE_SILENT_LOOPBACK;
}

static mp_uint_t machine_can_port_max_data_len(mp_uint_t flags) {
    (void)flags;
    return 8;
}

static uint32_t machine_can_c5_mode(machine_can_mode_t mode) {
    switch (mode) {
        case MP_CAN_MODE_NORMAL:
            return HAL_FDCAN_MODE_NORMAL;
        // C5 has no FDCAN sleep mode; match the STM32 backend's bus-monitoring mapping.
        case MP_CAN_MODE_SLEEP:
        case MP_CAN_MODE_SILENT:
            return HAL_FDCAN_MODE_BUS_MONITORING;
        case MP_CAN_MODE_LOOPBACK:
            return HAL_FDCAN_MODE_EXTERNAL_LOOPBACK;
        case MP_CAN_MODE_SILENT_LOOPBACK:
            return HAL_FDCAN_MODE_INTERNAL_LOOPBACK;
        default:
            return HAL_FDCAN_MODE_NORMAL;
    }
}

static void machine_can_c5_standby(bool standby) {
    #if defined(MICROPY_HW_CAN1_STANDBY)
    mp_hal_pin_obj_t pin = MICROPY_HW_CAN1_STANDBY;
    mp_hal_gpio_clock_enable(pin->gpio);
    bool level = standby ? !MICROPY_HW_CAN1_STANDBY_ACTIVE : MICROPY_HW_CAN1_STANDBY_ACTIVE;
    mp_hal_pin_write(pin, level);
    mp_hal_pin_output(pin);
    #else
    (void)standby;
    #endif
}

static void machine_can_port_clear_filters(machine_can_obj_t *self) {
    hal_fdcan_filter_t filter = {
        .filter_type = HAL_FDCAN_FILTER_TYPE_CLASSIC,
        .filter_config = HAL_FDCAN_FILTER_DISABLE,
    };
    for (uint32_t i = 0; i < CAN_C5_STD_FILTERS; ++i) {
        filter.id_type = HAL_FDCAN_ID_STANDARD;
        filter.filter_index = i;
        (void)HAL_FDCAN_SetFilter(&self->port->h, &filter);
    }
    for (uint32_t i = 0; i < CAN_C5_EXT_FILTERS; ++i) {
        filter.id_type = HAL_FDCAN_ID_EXTENDED;
        filter.filter_index = i;
        (void)HAL_FDCAN_SetFilter(&self->port->h, &filter);
    }
}

static void machine_can_port_set_filter(machine_can_obj_t *self, int filter_idx, mp_uint_t can_id, mp_uint_t mask, mp_uint_t flags) {
    bool ext = flags & CAN_MSG_FLAG_EXT_ID;
    if ((flags & ~CAN_MSG_FLAG_EXT_ID) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("flags"));
    }
    if (filter_idx >= (ext ? CAN_C5_EXT_FILTERS : CAN_C5_STD_FILTERS)) {
        mp_raise_ValueError(MP_ERROR_TEXT("too many filters for this ID type"));
    }
    hal_fdcan_filter_t filter = {
        .id_type = ext ? HAL_FDCAN_ID_EXTENDED : HAL_FDCAN_ID_STANDARD,
        .filter_index = filter_idx,
        .filter_type = HAL_FDCAN_FILTER_TYPE_CLASSIC,
        .filter_config = (filter_idx & 1) ? HAL_FDCAN_FILTER_TO_RX_FIFO_1 : HAL_FDCAN_FILTER_TO_RX_FIFO_0,
        .filter_id1 = can_id,
        .filter_id2 = mask,
    };
    if (HAL_FDCAN_SetFilter(&self->port->h, &filter) != HAL_OK) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CAN filter setup failed"));
    }
}

static void machine_can_port_set_filter_done(machine_can_obj_t *self) {
    (void)self;
}

static void machine_can_update_irqs(machine_can_obj_t *self) {
    const uint32_t fifo0 = HAL_FDCAN_IT_RX_FIFO_0_NEW_MSG | HAL_FDCAN_IT_RX_FIFO_0_FULL | HAL_FDCAN_IT_RX_FIFO_0_MSG_LOST;
    const uint32_t fifo1 = HAL_FDCAN_IT_RX_FIFO_1_NEW_MSG | HAL_FDCAN_IT_RX_FIFO_1_FULL | HAL_FDCAN_IT_RX_FIFO_1_MSG_LOST;
    if (self->mp_irq_trigger & MP_CAN_IRQ_RX) {
        (void)HAL_FDCAN_EnableInterrupts(&self->port->h, fifo0 | fifo1);
    } else {
        (void)HAL_FDCAN_DisableInterrupts(&self->port->h, fifo0 | fifo1);
    }
    if (!(self->mp_irq_trigger & MP_CAN_IRQ_TX)) {
        for (unsigned i = 0; i < CAN_TX_QUEUE_LEN; ++i) {
            if (self->port->tx_done & (1U << i)) {
                self->port->tx_id[i] = CAN_C5_TX_EMPTY;
            }
        }
        self->port->tx_done = self->port->tx_failed = 0;
    }
}

static void machine_can_port_init(machine_can_obj_t *self) {
    struct machine_can_port *port = self->port;
    if (port == NULL) {
        port = m_new0(struct machine_can_port, 1);
        self->port = port;
    } else {
        memset(port, 0, sizeof(*port));
    }
    for (size_t i = 0; i < CAN_TX_QUEUE_LEN; ++i) {
        port->tx_id[i] = CAN_C5_TX_EMPTY;
    }
    machine_can_c5_obj = self;

    const machine_pin_obj_t *tx_pin = MICROPY_HW_CAN1_TX;
    const machine_pin_obj_t *rx_pin = MICROPY_HW_CAN1_RX;
    #if defined(MICROPY_HW_CAN1_FDCAN_ID) && MICROPY_HW_CAN1_FDCAN_ID == 2
    const uint8_t can_unit = 2;
    #else
    const uint8_t can_unit = 1;
    #endif
    if (!mp_hal_pin_config_alt(tx_pin, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_UP, AF_FN_CAN, can_unit)
        || !mp_hal_pin_config_alt(rx_pin, MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_UP, AF_FN_CAN, can_unit)) {
        machine_can_c5_standby(true);
        machine_can_c5_obj = NULL;
        self->port = NULL;
        m_free(port);
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CAN pin configuration failed"));
    }
    HAL_RCC_FDCAN_EnableClock();
    port->regs = (FDCAN_GlobalTypeDef *)machine_can_c5_instance();
    if (HAL_FDCAN_Init(&port->h, machine_can_c5_instance()) != HAL_OK) {
        goto fail;
    }


    hal_fdcan_config_t config = {
        .nominal_bit_timing = {
            .nominal_prescaler = self->brp,
            .nominal_jump_width = self->sjw,
            .nominal_time_seg1 = self->tseg1,
            .nominal_time_seg2 = self->tseg2,
        },
        .mode = machine_can_c5_mode(self->mode),
        .frame_format = HAL_FDCAN_FRAME_FORMAT_CLASSIC_CAN,
        .auto_retransmission = HAL_FDCAN_AUTO_RETRANSMISSION_ENABLE,
        .transmit_pause = HAL_FDCAN_TRANSMIT_PAUSE_DISABLE,
        .protocol_exception = HAL_FDCAN_PROTOCOL_EXCEPTION_ENABLE,
        .std_filters_nbr = CAN_C5_STD_FILTERS,
        .ext_filters_nbr = CAN_C5_EXT_FILTERS,
        .tx_fifo_queue_mode = HAL_FDCAN_TX_MODE_QUEUE,
    };
    if (HAL_FDCAN_SetConfig(&port->h, &config) != HAL_OK) {
        goto fail;
    }
    hal_fdcan_global_filter_config_t global_filter = {
        .acceptance_non_matching_std = HAL_FDCAN_NO_MATCH_REJECT,
        .acceptance_non_matching_ext = HAL_FDCAN_NO_MATCH_REJECT,
        .acceptance_remote_std = HAL_FDCAN_REMOTE_ACCEPT,
        .acceptance_remote_ext = HAL_FDCAN_REMOTE_ACCEPT,
    };
    if (HAL_FDCAN_SetGlobalFilter(&port->h, &global_filter) != HAL_OK) {
        goto fail;
    }
    machine_can_port_clear_filters(self);
    if (HAL_FDCAN_SetInterruptGroupsToLine(&port->h,
        HAL_FDCAN_IT_GROUP_RX_FIFO_0 | HAL_FDCAN_IT_GROUP_STATUS_MSG | HAL_FDCAN_IT_GROUP_PROTOCOL_ERROR,
        HAL_FDCAN_IT_LINE_0) != HAL_OK
        || HAL_FDCAN_SetInterruptGroupsToLine(&port->h, HAL_FDCAN_IT_GROUP_RX_FIFO_1, HAL_FDCAN_IT_LINE_1) != HAL_OK) {
        goto fail;
    }
    // Buffer-specific enables do not enable the global TC/TCF interrupt sources.
    const uint32_t interrupts = HAL_FDCAN_IT_ERROR_WARNING | HAL_FDCAN_IT_ERROR_PASSIVE
        | HAL_FDCAN_IT_BUS_OFF | HAL_FDCAN_IT_TX_COMPLETE | HAL_FDCAN_IT_TX_ABORT_COMPLETE;
    if (HAL_FDCAN_EnableInterrupts(&port->h, interrupts) != HAL_OK
        || HAL_FDCAN_EnableTxBufferCompleteInterrupts(&port->h, HAL_FDCAN_IT_TX_CPLT_BUFFER_ALL) != HAL_OK
        || HAL_FDCAN_EnableTxBufferCancellationInterrupts(&port->h, HAL_FDCAN_IT_TX_ABORT_BUFFER_ALL) != HAL_OK
        || HAL_FDCAN_Start(&port->h) != HAL_OK) {
        goto fail;
    }
    if (HAL_FDCAN_EnableInterruptLines(&port->h, HAL_FDCAN_IT_LINE_0 | HAL_FDCAN_IT_LINE_1) != HAL_OK) {
        goto fail;
    }

    #if defined(MICROPY_HW_CAN1_FDCAN_ID) && MICROPY_HW_CAN1_FDCAN_ID == 2
    NVIC_SetPriority(FDCAN2_IT0_IRQn, IRQ_PRI_CAN);
    NVIC_EnableIRQ(FDCAN2_IT0_IRQn);
    NVIC_SetPriority(FDCAN2_IT1_IRQn, IRQ_PRI_CAN);
    NVIC_EnableIRQ(FDCAN2_IT1_IRQn);
    #else
    NVIC_SetPriority(FDCAN1_IT0_IRQn, IRQ_PRI_CAN);
    NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
    NVIC_SetPriority(FDCAN1_IT1_IRQn, IRQ_PRI_CAN);
    NVIC_EnableIRQ(FDCAN1_IT1_IRQn);
    #endif
    machine_can_c5_standby(false);
    return;

fail:
    machine_can_c5_standby(true);
    HAL_FDCAN_DeInit(&port->h);
    HAL_RCC_FDCAN_DisableClock();
    machine_can_c5_obj = NULL;
    self->port = NULL;
    m_free(port);
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CAN init failed"));
}

static void machine_can_port_deinit(machine_can_obj_t *self) {
    struct machine_can_port *port = self->port;
    machine_can_c5_standby(true);
    #if defined(MICROPY_HW_CAN1_FDCAN_ID) && MICROPY_HW_CAN1_FDCAN_ID == 2
    NVIC_DisableIRQ(FDCAN2_IT0_IRQn);
    NVIC_DisableIRQ(FDCAN2_IT1_IRQn);
    #else
    NVIC_DisableIRQ(FDCAN1_IT0_IRQn);
    NVIC_DisableIRQ(FDCAN1_IT1_IRQn);
    #endif
    HAL_FDCAN_DeInit(&port->h);
    HAL_RCC_FDCAN_DisableClock();
    if (machine_can_c5_obj == self) {
        machine_can_c5_obj = NULL;
    }
    self->port = NULL;
    m_free(port);
}

static mp_int_t machine_can_port_send(machine_can_obj_t *self, mp_uint_t id, const byte *data, size_t data_len, mp_uint_t flags) {
    if (flags & (CAN_MSG_FLAG_FD_F | CAN_MSG_FLAG_BRS)) {
        return -1;
    }
    struct machine_can_port *port = self->port;
    for (size_t i = 0; i < CAN_TX_QUEUE_LEN; ++i) {
        if (port->tx_id[i] == id && !(flags & CAN_MSG_FLAG_UNORDERED)) {
            return -1;
        }
    }
    // Free-level reporting is FIFO-only; this controller uses priority queue mode.
    if (HAL_FDCAN_GetTxFifoStatus(&port->h) == HAL_FDCAN_FIFO_STATUS_FULL) {
        return -1;
    }
    hal_fdcan_tx_header_t header = { .b = {
                                         .identifier = id,
                                         .frame_type = (flags & CAN_MSG_FLAG_RTR) ? HAL_FDCAN_FRAME_REMOTE : HAL_FDCAN_FRAME_DATA,
                                         .identifier_type = (flags & CAN_MSG_FLAG_EXT_ID) ? HAL_FDCAN_ID_EXTENDED : HAL_FDCAN_ID_STANDARD,
                                         .error_state_indicator = HAL_FDCAN_ERROR_STATE_IND_ACTIVE,
                                         .data_length = (hal_fdcan_data_length_code_t)data_len,
                                         .bit_rate_switch = HAL_FDCAN_BIT_RATE_SWITCH_OFF,
                                         .frame_format = HAL_FDCAN_HEADER_FRAME_FORMAT_CAN,
                                         .event_fifo_control = HAL_FDCAN_TX_EVENTS_FIFO_DISCARD,
                                     } };
    mp_uint_t irq_state = MICROPY_BEGIN_ATOMIC_SECTION();
    // Do not let the hardware reuse a completed slot before its completion index is consumed.
    if (port->tx_done != 0) {
        MICROPY_END_ATOMIC_SECTION(irq_state);
        return -1;
    }
    if (HAL_FDCAN_ReqTransmitMsgFromFIFOQ(&port->h, &header, (const uint8_t *)data) != HAL_OK) {
        MICROPY_END_ATOMIC_SECTION(irq_state);
        return -1;
    }
    uint32_t tx_buffer_mask = HAL_FDCAN_GetLatestTxFifoQRequestBuffer(&port->h);
    assert(tx_buffer_mask != 0);
    uint32_t idx = __builtin_ctz(tx_buffer_mask);
    assert(idx < CAN_TX_QUEUE_LEN);
    port->tx_id[idx] = id;
    port->tx_inflight |= tx_buffer_mask;
    MICROPY_END_ATOMIC_SECTION(irq_state);
    return idx;
}

static bool machine_can_port_cancel_send(machine_can_obj_t *self, mp_uint_t idx) {
    if (idx >= CAN_TX_QUEUE_LEN || !(self->port->regs->TXBRP & (1U << idx))) {
        return false;
    }
    return HAL_FDCAN_ReqAbortOfTxBuffer(&self->port->h, 1U << idx) == HAL_OK;
}

static bool machine_can_port_recv(machine_can_obj_t *self, void *data, size_t *dlen, mp_uint_t *id, mp_uint_t *flags, mp_uint_t *errors) {
    struct machine_can_port *port = self->port;
    for (hal_fdcan_rx_location_t fifo = HAL_FDCAN_RX_FIFO_0; fifo <= HAL_FDCAN_RX_FIFO_1; ++fifo) {
        uint32_t fill = 0;
        HAL_FDCAN_GetRxFifoFillLevel(&port->h, fifo, &fill);
        if (fill == 0) {
            continue;
        }
        hal_fdcan_rx_header_t header;
        if (HAL_FDCAN_GetReceivedMessage(&port->h, fifo, &header, data) != HAL_OK) {
            continue;
        }
        static const uint8_t dlc_to_len[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8, 8, 8, 8, 8 };
        *id = header.b.identifier;
        // The C5 HAL already normalizes standard identifiers.
        if (header.b.identifier_type == HAL_FDCAN_ID_EXTENDED) {
            *flags |= CAN_MSG_FLAG_EXT_ID;
        }
        if (header.b.frame_type == HAL_FDCAN_FRAME_REMOTE) {
            *flags |= CAN_MSG_FLAG_RTR;
        }
        if (header.b.error_state_indicator == HAL_FDCAN_ERROR_STATE_IND_PASSIVE) {
            self->rx_error_flags |= CAN_RECV_ERR_ESI;
        }
        *dlen = dlc_to_len[header.b.data_length];
        *errors = self->rx_error_flags;
        self->rx_error_flags = 0;
        machine_can_update_irqs(self);
        return true;
    }
    return false;
}

static bool machine_can_c5_tx_result(machine_can_obj_t *self, int *idx, bool *success) {
    struct machine_can_port *port = self->port;
    if (port->tx_done == 0) {
        return false;
    }
    *idx = __builtin_ctz((unsigned)port->tx_done);
    uint8_t bit = 1U << *idx;
    *success = !(port->tx_failed & bit);
    port->tx_done &= ~bit;
    port->tx_failed &= ~bit;
    port->tx_id[*idx] = CAN_C5_TX_EMPTY;
    return true;
}

static mp_uint_t machine_can_port_irq_flags(machine_can_obj_t *self) {
    struct machine_can_port *port = self->port;
    mp_uint_t flags = 0;
    if ((self->mp_irq_trigger & MP_CAN_IRQ_STATE) && port->irq_state_pending) {
        flags |= MP_CAN_IRQ_STATE;
        port->irq_state_pending = false;
    }
    if (self->mp_irq_trigger & MP_CAN_IRQ_RX) {
        uint32_t f0 = 0, f1 = 0;
        HAL_FDCAN_GetRxFifoFillLevel(&port->h, HAL_FDCAN_RX_FIFO_0, &f0);
        HAL_FDCAN_GetRxFifoFillLevel(&port->h, HAL_FDCAN_RX_FIFO_1, &f1);
        if (f0 || f1) {
            flags |= MP_CAN_IRQ_RX;
        }
    }
    if (self->mp_irq_trigger & MP_CAN_IRQ_TX) {
        int idx;
        bool success;
        if (machine_can_c5_tx_result(self, &idx, &success)) {
            flags |= MP_CAN_IRQ_TX | ((mp_uint_t)idx << MP_CAN_IRQ_IDX_SHIFT);
            if (!success) {
                flags |= MP_CAN_IRQ_TX_FAILED;
            }
        }
    }
    return flags;
}

static machine_can_state_t machine_can_port_get_state(machine_can_obj_t *self) {
    uint32_t psr = self->port->regs->PSR;
    if (psr & FDCAN_PSR_BO) {
        return MP_CAN_STATE_BUS_OFF;
    }
    if (psr & FDCAN_PSR_EP) {
        return MP_CAN_STATE_PASSIVE;
    }
    if (psr & FDCAN_PSR_EW) {
        return MP_CAN_STATE_WARNING;
    }
    return MP_CAN_STATE_ACTIVE;
}

static void machine_can_port_update_counters(machine_can_obj_t *self) {
    struct machine_can_port *port = self->port;
    machine_can_counters_t *counters = &self->counters;
    hal_fdcan_error_counters_t hw;
    if (HAL_FDCAN_GetErrorCounters(&port->h, &hw) == HAL_OK) {
        counters->tec = hw.tx_error_cnt;
        counters->rec = hw.rx_error_cnt;
    }
    counters->tx_pending = __builtin_popcount(port->regs->TXBRP & 0x7);
    uint32_t f0 = 0, f1 = 0;
    HAL_FDCAN_GetRxFifoFillLevel(&port->h, HAL_FDCAN_RX_FIFO_0, &f0);
    HAL_FDCAN_GetRxFifoFillLevel(&port->h, HAL_FDCAN_RX_FIFO_1, &f1);
    counters->rx_pending = f0 + f1;
}

static mp_obj_t machine_can_port_get_additional_timings(machine_can_obj_t *self, mp_obj_t optional_arg) {
    (void)self;
    (void)optional_arg;
    return mp_const_none;
}

static void machine_can_port_restart(machine_can_obj_t *self) {
    (void)HAL_FDCAN_Recover(&self->port->h);
}

void machine_can_c5_irq_handler(void) {
    if (machine_can_c5_obj != NULL) {
        HAL_FDCAN_IRQHandler(&machine_can_c5_obj->port->h);
    }
}

static void machine_can_c5_notify_rx(hal_fdcan_handle_t *hfdcan, uint32_t interrupt_flags) {
    (void)hfdcan;
    machine_can_obj_t *self = machine_can_c5_obj;
    if (self == NULL) {
        return;
    }
    if (interrupt_flags & (HAL_FDCAN_FLAG_RX_FIFO_0_FULL | HAL_FDCAN_FLAG_RX_FIFO_1_FULL)) {
        self->rx_error_flags |= CAN_RECV_ERR_FULL;
    }
    if (interrupt_flags & (HAL_FDCAN_FLAG_RX_FIFO_0_MSG_LOST | HAL_FDCAN_FLAG_RX_FIFO_1_MSG_LOST)) {
        self->rx_error_flags |= CAN_RECV_ERR_OVERRUN;
        self->counters.rx_overruns++;
    }
    if ((self->mp_irq_trigger & MP_CAN_IRQ_RX) && self->mp_irq_obj != NULL) {
        mp_irq_handler(self->mp_irq_obj);
    }
}

void HAL_FDCAN_RxFifo0Callback(hal_fdcan_handle_t *hfdcan, uint32_t interrupt_flags) {
    machine_can_c5_notify_rx(hfdcan, interrupt_flags);
}

void HAL_FDCAN_RxFifo1Callback(hal_fdcan_handle_t *hfdcan, uint32_t interrupt_flags) {
    machine_can_c5_notify_rx(hfdcan, interrupt_flags);
}

static void machine_can_c5_tx_finished(uint32_t buffers, bool failed) {
    machine_can_obj_t *self = machine_can_c5_obj;
    if (self == NULL) {
        return;
    }
    // TXBTO/TXBCF retain older results until a new request uses that slot.
    uint8_t mask = buffers & self->port->tx_inflight;
    self->port->tx_inflight &= ~mask;
    if (self->mp_irq_trigger & MP_CAN_IRQ_TX) {
        self->port->tx_done |= mask;
        if (failed) {
            self->port->tx_failed |= mask;
        }
        if (self->mp_irq_obj != NULL) {
            // One hardware interrupt may complete several indexed requests.
            for (unsigned pending = mask; pending; pending &= pending - 1) {
                mp_irq_handler(self->mp_irq_obj);
            }
        }
    } else {
        for (int i = 0; i < CAN_TX_QUEUE_LEN; ++i) {
            if (mask & (1U << i)) {
                self->port->tx_id[i] = CAN_C5_TX_EMPTY;
            }
        }
    }
}

void HAL_FDCAN_TxBufferCompleteCallback(hal_fdcan_handle_t *hfdcan, uint32_t tx_buffers_idx) {
    (void)hfdcan;
    machine_can_c5_tx_finished(tx_buffers_idx, false);
}

void HAL_FDCAN_TxBufferAbortCallback(hal_fdcan_handle_t *hfdcan, uint32_t tx_buffers_idx) {
    (void)hfdcan;
    machine_can_c5_tx_finished(tx_buffers_idx, true);
}

void HAL_FDCAN_ErrorCallback(hal_fdcan_handle_t *hfdcan) {
    (void)hfdcan;
    machine_can_obj_t *self = machine_can_c5_obj;
    if (self == NULL) {
        return;
    }
    uint32_t psr = self->port->regs->PSR;
    bool passive = (psr & FDCAN_PSR_EP) != 0;
    bool warning = (psr & FDCAN_PSR_EW) != 0;
    bool bus_off = (psr & FDCAN_PSR_BO) != 0;
    bool changed = false;
    if (bus_off) {
        self->counters.num_bus_off++;
        changed = true;
    } else if (passive && !self->port->error_passive) {
        self->counters.num_passive++;
        changed = true;
    } else if (warning && !self->port->error_passive) {
        self->counters.num_warning++;
        changed = true;
    }
    self->port->error_passive = passive;
    if (changed && (self->mp_irq_trigger & MP_CAN_IRQ_STATE)) {
        self->port->irq_state_pending = true;
        if (self->mp_irq_obj != NULL) {
            mp_irq_handler(self->mp_irq_obj);
        }
    }
}
