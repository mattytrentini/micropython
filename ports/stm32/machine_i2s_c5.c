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

// C5 implementation included by extmod/machine_i2s.c.
#include <string.h>
#include "py/mphal.h"
#include "pin.h"
#include "dma.h"
#include "stm32c5xx_hal_i2s.h"
#include "stm32c5xx_hal_rcc.h"

#define I2S_DMA_BUFFER_SIZE (256)
#define I2S_DMA_HALF_SIZE (I2S_DMA_BUFFER_SIZE / 2)
#define I2S_DMA_IRQ_FLAGS (LL_DMA_IT_HT | LL_DMA_IT_TC | LL_DMA_IT_DTE | LL_DMA_IT_ULE | LL_DMA_IT_USE | LL_DMA_IT_TO)
#define NUM_I2S_USER_FORMATS (4)
#define I2S_RX_FRAME_SIZE_IN_BYTES (8)
#define SIZEOF_NON_BLOCKING_COPY_IN_BYTES (512)

static const int8_t i2s_frame_map[NUM_I2S_USER_FORMATS][I2S_RX_FRAME_SIZE_IN_BYTES] = {
    { 0, 1, -1, -1, -1, -1, -1, -1 },
    { 2, 3, 0, 1, -1, -1, -1, -1 },
    { 0, 1, -1, -1, 2, 3, -1, -1 },
    { 2, 3, 0, 1, 6, 7, 4, 5 },
};

static int8_t get_frame_mapping_index(int8_t bits, format_t format) {
    if (format == MONO) {
        return bits == 16 ? 0 : 1;
    }
    return bits == 16 ? 2 : 3;
}

typedef struct _machine_i2s_obj_t {
    mp_obj_base_t base;
    uint8_t i2s_id;
    mp_hal_pin_obj_t sck;
    mp_hal_pin_obj_t ws;
    mp_hal_pin_obj_t sd;
    uint16_t mode;
    int8_t bits;
    format_t format;
    int32_t rate;
    int32_t ibuf;
    mp_obj_t callback_for_non_blocking;
    uint8_t dma_buffer[I2S_DMA_BUFFER_SIZE + 0x1f];
    uint8_t *dma_buffer_dcache_aligned;
    ring_buf_t ring_buffer;
    uint8_t *ring_buffer_storage;
    non_blocking_descriptor_t non_blocking_descriptor;
    io_mode_t io_mode;
    hal_i2s_handle_t hi2s;
    const dma_descr_t *dma_descr;
    bool dma_initialized;
} machine_i2s_obj_t;

static uint32_t i2s_frequency(int32_t rate) {
    switch (rate) {
        case 8000:
            return HAL_I2S_MASTER_AUDIO_FREQ_8_KHZ;
        case 11025:
            return HAL_I2S_MASTER_AUDIO_FREQ_11_KHZ;
        case 16000:
            return HAL_I2S_MASTER_AUDIO_FREQ_16_KHZ;
        case 22050:
            return HAL_I2S_MASTER_AUDIO_FREQ_22_KHZ;
        case 32000:
            return HAL_I2S_MASTER_AUDIO_FREQ_32_KHZ;
        case 44100:
            return HAL_I2S_MASTER_AUDIO_FREQ_44_KHZ;
        case 48000:
            return HAL_I2S_MASTER_AUDIO_FREQ_48_KHZ;
        case 96000:
            return HAL_I2S_MASTER_AUDIO_FREQ_96_KHZ;
        case 192000:
            return HAL_I2S_MASTER_AUDIO_FREQ_192_KHZ;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported I2S rate"));
    }
}

static const pin_af_obj_t *i2s_validate_pin(mp_obj_t pin_obj, uint8_t type, uint8_t id) {
    if (!mp_obj_is_type(pin_obj, &pin_type)) {
        mp_raise_ValueError(MP_ERROR_TEXT("I2S pin must be a Pin"));
    }
    const pin_af_obj_t *af = pin_find_af(MP_OBJ_TO_PTR(pin_obj), AF_FN_I2S, id);
    if (af == NULL || af->type != type) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid I2S pin"));
    }
    return af;
}

static void i2s_set_pin(mp_hal_pin_obj_t pin, const pin_af_obj_t *af) {
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = pin->pin_mask;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = af->idx;
    HAL_GPIO_Init(pin->gpio, &gpio);
}

static void i2s_init_pins(machine_i2s_obj_t *self) {
    const pin_af_obj_t *sck = i2s_validate_pin(MP_OBJ_FROM_PTR(self->sck), AF_PIN_TYPE_I2S_CK, self->i2s_id);
    const pin_af_obj_t *ws = i2s_validate_pin(MP_OBJ_FROM_PTR(self->ws), AF_PIN_TYPE_I2S_WS, self->i2s_id);
    const pin_af_obj_t *sd = i2s_validate_pin(MP_OBJ_FROM_PTR(self->sd), AF_PIN_TYPE_I2S_SD, self->i2s_id);
    i2s_set_pin(self->sck, sck);
    i2s_set_pin(self->ws, ws);
    i2s_set_pin(self->sd, sd);
}

static uint32_t i2s_reformat_word(uint32_t sample) {
    return (sample << 16) | (sample >> 16);
}

static void i2s_empty_half(machine_i2s_obj_t *self, size_t offset) {
    uint8_t *buffer = self->dma_buffer_dcache_aligned + offset;
    MP_HAL_CLEANINVALIDATE_DCACHE(buffer, I2S_DMA_HALF_SIZE);
    if (ringbuf_available_space(&self->ring_buffer) >= I2S_DMA_HALF_SIZE) {
        for (size_t i = 0; i < I2S_DMA_HALF_SIZE; ++i) {
            ringbuf_push(&self->ring_buffer, buffer[i]);
        }
    }
}

static void i2s_feed_half(machine_i2s_obj_t *self, size_t offset) {
    uint8_t *buffer = self->dma_buffer_dcache_aligned + offset;
    size_t sample_bytes = self->bits / 8;
    size_t user_bytes = I2S_DMA_HALF_SIZE;
    if (self->format == MONO) {
        user_bytes /= 2;
    }

    if (ringbuf_available_data(&self->ring_buffer) >= user_bytes) {
        if (self->format == MONO) {
            for (size_t i = 0; i < user_bytes; i += sample_bytes) {
                for (size_t j = 0; j < sample_bytes; ++j) {
                    ringbuf_pop(&self->ring_buffer, &buffer[(i * 2) + j]);
                }
                memcpy(buffer + i * 2 + sample_bytes, buffer + i * 2, sample_bytes);
            }
        } else {
            for (size_t i = 0; i < user_bytes; ++i) {
                ringbuf_pop(&self->ring_buffer, &buffer[i]);
            }
        }
        if (self->bits == 32) {
            for (size_t i = 0; i < I2S_DMA_HALF_SIZE / sizeof(uint32_t); ++i) {
                uint32_t sample;
                memcpy(&sample, buffer + i * sizeof(sample), sizeof(sample));
                sample = i2s_reformat_word(sample);
                memcpy(buffer + i * sizeof(sample), &sample, sizeof(sample));
            }
        }
    } else {
        memset(buffer, 0, I2S_DMA_HALF_SIZE);
    }
    MP_HAL_CLEAN_DCACHE(buffer, I2S_DMA_HALF_SIZE);
}

static void i2s_dma_irq(const dma_descr_t *descr, uint32_t flags) {
    machine_i2s_obj_t *self = MP_STATE_PORT(machine_i2s_obj)[0];
    if (self == NULL || !self->dma_initialized || self->dma_descr != descr) {
        return;
    }
    if (flags & (LL_DMA_FLAG_DTE | LL_DMA_FLAG_ULE | LL_DMA_FLAG_USE | LL_DMA_FLAG_TO)) {
        return;
    }
    size_t offset = (flags & LL_DMA_FLAG_HT) ? 0 : I2S_DMA_HALF_SIZE;
    if (self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_TX) {
        i2s_feed_half(self, offset);
        if (self->io_mode == NON_BLOCKING && self->non_blocking_descriptor.copy_in_progress) {
            copy_appbuf_to_ringbuf_non_blocking(self);
        }
    } else {
        i2s_empty_half(self, offset);
        if (self->io_mode == NON_BLOCKING && self->non_blocking_descriptor.copy_in_progress) {
            fill_appbuf_from_ringbuf_non_blocking(self);
        }
    }
}


void machine_i2s_init0(void) {
    for (uint8_t i = 0; i < MICROPY_HW_MAX_I2S; ++i) {
        machine_i2s_obj_t *self = MP_STATE_PORT(machine_i2s_obj)[i];
        if (self != NULL) {
            mp_machine_i2s_deinit(self);
            MP_STATE_PORT(machine_i2s_obj)[i] = NULL;
        }
    }
}

static bool i2s_init(machine_i2s_obj_t *self) {
    if (self->i2s_id != 1) {
        return false;
    }

    self->dma_descr = self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_TX ? &dma_I2S_1_TX : &dma_I2S_1_RX;

    i2s_init_pins(self);

    if (HAL_RCC_SPI1_SetKernelClkSource(HAL_RCC_SPI1_CLK_SRC_PCLK2) != HAL_OK) {
        return false;
    }
    if (HAL_I2S_Init(&self->hi2s, HAL_I2S1) != HAL_OK) {
        return false;
    }

    // Receive mapping consumes 32-bit wire slots, including for 16-bit users.
    bool slots_32_bit = self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_RX || self->bits == 32;
    hal_i2s_master_config_t config = {
        .mode = self->mode,
        .standard = HAL_I2S_STANDARD_PHILIPS,
        .data_format = slots_32_bit ? HAL_I2S_DATA_FORMAT_32_BIT : HAL_I2S_DATA_FORMAT_16_BIT,
        .clock_polarity = HAL_I2S_CLOCK_POLARITY_LOW,
        .bit_order = HAL_I2S_MSB_FIRST,
        .audio_frequency = i2s_frequency(self->rate),
    };
    if (HAL_I2S_MASTER_SetConfig(&self->hi2s, &config) != HAL_OK) {
        HAL_I2S_DeInit(&self->hi2s);
        return false;
    }

    uint32_t width = config.data_format == HAL_I2S_DATA_FORMAT_16_BIT
        ? DMA_CTR1_SDW_LOG2_0 | DMA_CTR1_DDW_LOG2_0
        : DMA_CTR1_SDW_LOG2_1 | DMA_CTR1_DDW_LOG2_1;
    uint32_t ctr1 = width;
    if (self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_TX) {
        ctr1 |= DMA_CTR1_SINC;
    } else {
        ctr1 |= DMA_CTR1_DINC;
    }
    int ret = dma_init(self->dma_descr, ctr1, 0, DMA_CIRCULAR);
    if (ret != 0) {
        HAL_I2S_DeInit(&self->hi2s);
        return false;
    }
    self->dma_initialized = true;

    if (dma_set_irq(self->dma_descr, i2s_dma_irq, I2S_DMA_IRQ_FLAGS) != 0) {
        dma_deinit(self->dma_descr);
        self->dma_initialized = false;
        HAL_I2S_DeInit(&self->hi2s);
        return false;
    }

    if (self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_TX) {
        i2s_feed_half(self, 0);
        i2s_feed_half(self, I2S_DMA_HALF_SIZE);
    } else {
        memset(self->dma_buffer_dcache_aligned, 0, I2S_DMA_BUFFER_SIZE);
        MP_HAL_CLEAN_DCACHE(self->dma_buffer_dcache_aligned, I2S_DMA_BUFFER_SIZE);
    }

    SPI_TypeDef *spi = (SPI_TypeDef *)(uintptr_t)self->hi2s.instance;
    volatile void *data_register = self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_TX
        ? (volatile void *)&spi->TXDR : (volatile void *)&spi->RXDR;
    const void *source = self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_TX
        ? (const void *)self->dma_buffer_dcache_aligned : (const void *)data_register;
    void *destination = self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_TX
        ? (void *)data_register : (void *)self->dma_buffer_dcache_aligned;
    ret = dma_start_ping_pong(self->dma_descr, source, destination, I2S_DMA_HALF_SIZE);
    if (ret != 0) {
        dma_deinit(self->dma_descr);
        self->dma_initialized = false;
        HAL_I2S_DeInit(&self->hi2s);
        mp_raise_OSError(-ret);
    }

    if (self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_TX) {
        LL_I2S_EnableDMAReq_TX(spi);
    } else {
        LL_I2S_EnableDMAReq_RX(spi);
    }
    LL_I2S_Enable(spi);
    LL_I2S_StartTransfer(spi);
    return true;
}

static void mp_machine_i2s_init_helper(machine_i2s_obj_t *self, mp_arg_val_t *args) {
    int mode = args[ARG_mode].u_int;
    int bits = args[ARG_bits].u_int;
    int format = args[ARG_format].u_int;
    int ibuf = args[ARG_ibuf].u_int;
    if (mode != MICROPY_PY_MACHINE_I2S_CONSTANT_RX && mode != MICROPY_PY_MACHINE_I2S_CONSTANT_TX) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid mode"));
    }
    if (bits != 16 && bits != 32) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid bits"));
    }
    if (format != MONO && format != STEREO) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid format"));
    }
    if (ibuf <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid ibuf"));
    }
    int32_t rate = args[ARG_rate].u_int;
    (void)i2s_frequency(rate);
    i2s_validate_pin(args[ARG_sck].u_obj, AF_PIN_TYPE_I2S_CK, 1);
    i2s_validate_pin(args[ARG_ws].u_obj, AF_PIN_TYPE_I2S_WS, 1);
    i2s_validate_pin(args[ARG_sd].u_obj, AF_PIN_TYPE_I2S_SD, 1);

    self->sck = MP_OBJ_TO_PTR(args[ARG_sck].u_obj);
    self->ws = MP_OBJ_TO_PTR(args[ARG_ws].u_obj);
    self->sd = MP_OBJ_TO_PTR(args[ARG_sd].u_obj);
    self->mode = mode;
    self->bits = bits;
    self->format = format;
    self->rate = args[ARG_rate].u_int;
    self->ibuf = ibuf;
    self->callback_for_non_blocking = MP_OBJ_NULL;
    self->non_blocking_descriptor.copy_in_progress = false;
    self->io_mode = BLOCKING;
    memset(&self->hi2s, 0, sizeof(self->hi2s));
    self->dma_initialized = false;
    self->dma_descr = NULL;

    if (self->ring_buffer_storage != NULL) {
        m_free(self->ring_buffer_storage);
    }
    self->ring_buffer_storage = m_new(uint8_t, ibuf);
    ringbuf_init(&self->ring_buffer, self->ring_buffer_storage, ibuf);
    self->dma_buffer_dcache_aligned = (uint8_t *)(((uintptr_t)self->dma_buffer + 0x1f) & ~(uintptr_t)0x1f);
    if (!i2s_init(self)) {
        mp_machine_i2s_deinit(self);
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("I2S init failed"));
    }
}

static machine_i2s_obj_t *mp_machine_i2s_make_new_instance(mp_int_t i2s_id) {
    if (i2s_id != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid id"));
    }
    machine_i2s_obj_t *self = MP_STATE_PORT(machine_i2s_obj)[0];
    if (self == NULL) {
        self = mp_obj_malloc(machine_i2s_obj_t, &machine_i2s_type);
        MP_STATE_PORT(machine_i2s_obj)[0] = self;
        self->i2s_id = 1;
        self->ring_buffer_storage = NULL;
        self->dma_initialized = false;
        self->dma_descr = NULL;
    } else {
        mp_machine_i2s_deinit(self);
    }
    return self;
}

static void mp_machine_i2s_deinit(machine_i2s_obj_t *self) {
    if (self->dma_initialized) {
        SPI_TypeDef *spi = (SPI_TypeDef *)(uintptr_t)self->hi2s.instance;
        if (self->mode == MICROPY_PY_MACHINE_I2S_CONSTANT_TX) {
            LL_I2S_DisableDMAReq_TX(spi);
        } else {
            LL_I2S_DisableDMAReq_RX(spi);
        }
        LL_I2S_Disable(spi);
        int ret = dma_deinit(self->dma_descr);
        if (ret != 0) {
            mp_raise_OSError(-ret);
        }
        self->dma_initialized = false;
    }
    if (self->hi2s.global_state != HAL_I2S_STATE_RESET) {
        HAL_I2S_DeInit(&self->hi2s);
    }
    if (self->ring_buffer_storage != NULL) {
        m_free(self->ring_buffer_storage);
        self->ring_buffer_storage = NULL;
    }
}

static void mp_machine_i2s_irq_update(machine_i2s_obj_t *self) {
    (void)self;
}
MP_REGISTER_ROOT_POINTER(struct _machine_i2s_obj_t *machine_i2s_obj[MICROPY_HW_MAX_I2S]);
