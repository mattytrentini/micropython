from machine import I2C
import rtc8563

_rtc = rtc8563.RTC8563()


def rtc_enable():
    _rtc.enable(I2C(1))


def rtc_read() -> rtc8563.DateTime:
    return _rtc.get_datetime()


def rtc_set(datetime: rtc8563.DateTime):
    _rtc.set_datetime(datetime)
