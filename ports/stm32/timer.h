/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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
#ifndef MICROPY_INCLUDED_STM32_TIMER_H
#define MICROPY_INCLUDED_STM32_TIMER_H

#if defined(STM32C5)
// Native C5 state, including the configuration exposed through pyb.Timer.
typedef struct {
    uint32_t Prescaler;
    uint32_t CounterMode;
    uint32_t Period;
    uint32_t ClockDivision;
    uint32_t RepetitionCounter;
} timer_init_t;

typedef struct {
    TIM_TypeDef *Instance;
    timer_init_t Init;
    bool State;
} timer_handle_t;
#else
typedef TIM_Base_InitTypeDef timer_init_t;
typedef TIM_HandleTypeDef timer_handle_t;
#endif

// Define this helper macro for MCUs that the HAL misses.
#if defined(STM32L0)
#define IS_TIM_32B_COUNTER_INSTANCE(tim) (false)
#endif

extern timer_handle_t servo_tim_handle;

extern const mp_obj_type_t pyb_timer_type;

void timer_init0(void);
void timer_servo_init(void);
timer_handle_t *timer_tim6_init(uint freq);
void timer_deinit(void);
TIM_TypeDef *timer_id_to_reg(uint32_t tim_id);
void timer_clock_enable(size_t tim_id);
uint32_t timer_get_source_freq(uint32_t tim_id);
void timer_irq_handler(uint tim_id);

timer_handle_t *pyb_timer_get_handle(mp_obj_t timer);

#endif // MICROPY_INCLUDED_STM32_TIMER_H
