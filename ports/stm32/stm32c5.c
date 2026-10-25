// Lifecycle hooks for peripheral drivers not yet ported to STM32C5.

#include "py/runtime.h"
#include "py/mphal.h"
#include STM32_HAL_H
#include "boards/stm32c5xx_hal_compat.h"
#include "rtc.h"
#include "shared/timeutils/timeutils.h"

// RTC wakeup is shared by the machine low-power entry points.
mp_obj_t pyb_rtc_wakeup(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    mp_raise_NotImplementedError(MP_ERROR_TEXT("RTC wakeup is not supported on STM32C5"));
}

// Nanoseconds since the MicroPython epoch, derived from the RTC calendar.
uint64_t mp_hal_time_ns(void) {
    #if MICROPY_HW_ENABLE_RTC
    rtc_init_finalise();
    hal_rtc_date_t date;
    hal_rtc_time_t time;
    if (HAL_RTC_CALENDAR_GetDateTime(&date, &time) != HAL_OK) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("RTC read failed"));
    }
    hal_rtc_calendar_config_t calendar;
    HAL_RTC_CALENDAR_GetConfig(&calendar);
    if (calendar.hour_format == HAL_RTC_CALENDAR_HOUR_FORMAT_AMPM) {
        time.hour = time.am_pm == HAL_RTC_TIME_FORMAT_PM
            ? (time.hour == 12 ? 12 : time.hour + 12)
            : (time.hour == 12 ? 0 : time.hour);
    }
    hal_rtc_config_t config;
    HAL_RTC_GetConfig(&config);
    uint64_t ns = (uint64_t)timeutils_seconds_since_epoch(
        2000 + date.year, date.mon, date.mday, time.hour, time.min, time.sec) * 1000000000ULL;
    ns += (uint64_t)(config.synch_prediv - time.subsec) * 1000000000ULL / (config.synch_prediv + 1);
    return ns;
    #else
    mp_raise_NotImplementedError(MP_ERROR_TEXT("RTC time is not supported on STM32C5"));
    #endif
}

