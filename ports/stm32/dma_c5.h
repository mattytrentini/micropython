// Native STM32C5 LPDMA backend.
// SPDX-License-Identifier: MIT

#ifndef MICROPY_INCLUDED_STM32_DMA_C5_H
#define MICROPY_INCLUDED_STM32_DMA_C5_H

#include <stddef.h>
#include <stdint.h>
#include "stm32c5xx_ll_dma.h"

// Match the port's software circular-mode flag; it is not a CCR hardware bit.
#define DMA_CIRCULAR (0x20000000u)

typedef struct _dma_descr_t {
    uint8_t channel; // 0..7: LPDMA1; 8..15: LPDMA2.
    uint8_t request;
} dma_descr_t;

typedef void (*dma_irq_callback_t)(const dma_descr_t *descr, uint32_t flags);

extern const dma_descr_t dma_SPI_1_RX;
extern const dma_descr_t dma_SPI_1_TX;
extern const dma_descr_t dma_SPI_2_RX;
extern const dma_descr_t dma_SPI_2_TX;
extern const dma_descr_t dma_SPI_3_RX;
extern const dma_descr_t dma_SPI_3_TX;
extern const dma_descr_t dma_ADC_1_RX;
extern const dma_descr_t dma_ADC_2_RX;
extern const dma_descr_t dma_ADC_3_RX;
extern const dma_descr_t dma_DAC_1_TX;
extern const dma_descr_t dma_I2C_1_RX;
extern const dma_descr_t dma_I2C_1_TX;
extern const dma_descr_t dma_I2C_2_RX;
extern const dma_descr_t dma_I2C_2_TX;
extern const dma_descr_t dma_I2S_1_RX;
extern const dma_descr_t dma_I2S_1_TX;

void dma_init0(void);
int dma_deinit_all(void);

// Claims a channel exclusively. CTR1/CTR2 are native register configurations;
// request selection comes from descr. Config accepts DMA_CIRCULAR.
int dma_init(const dma_descr_t *descr, uint32_t ctr1, uint32_t ctr2, uint32_t config);
int dma_start(const dma_descr_t *descr, const void *src, void *dest, size_t nbytes);
// Starts a circular transfer over two adjacent equal-sized halves. Configure
// dma_set_irq() with LL_DMA_IT_HT | LL_DMA_IT_TC for callbacks at both
// boundaries; callback flags identify the completed half (HT=first, TC=second).
int dma_start_ping_pong(const dma_descr_t *descr, const void *src, void *dest, size_t half_nbytes);
// Returns -EAGAIN while running, zero on completion, or a negative errno.
// Circular completion consumes one block event, not ownership of the buffer.
int dma_status(const dma_descr_t *descr);
// Optional allocation-free hard-IRQ callback; flags use native CSR/CCR masks.
int dma_set_irq(const dma_descr_t *descr, dma_irq_callback_t callback, uint32_t flags);
int dma_abort(const dma_descr_t *descr);
// Buffers, descriptors and callbacks must remain alive until this succeeds.
// On timeout the channel stays owned: neither its node nor its clock is freed.
int dma_deinit(const dma_descr_t *descr);

// Shared dispatcher called by the LPDMA source-vector handlers.
void dma_irq_handler(uint32_t channel_index);

#endif // MICROPY_INCLUDED_STM32_DMA_C5_H
