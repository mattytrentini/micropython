/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 MicroPython contributors
 */

#ifndef MICROPY_INCLUDED_WEACTSTUDIO_STM32C552_STM32C5XX_HAL_CONF_H
#define MICROPY_INCLUDED_WEACTSTUDIO_STM32C552_STM32C5XX_HAL_CONF_H

#define HSE_VALUE (8000000)
#define HSE_STARTUP_TIMEOUT (100)
#define LSE_VALUE (32768)
#define LSE_STARTUP_TIMEOUT (5000)

#include "boards/stm32c5xx_hal_conf_base.h"

#endif // MICROPY_INCLUDED_WEACTSTUDIO_STM32C552_STM32C5XX_HAL_CONF_H
