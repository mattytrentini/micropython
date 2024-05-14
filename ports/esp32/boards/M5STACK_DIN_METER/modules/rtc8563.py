from machine import I2C, SoftI2C

try:
    # Macropython
    from collections import namedtuple
except ImportError:
    # MicroPython
    from ucollections import namedtuple

try:
    from typing import Optional, Union
except ImportError:
    # MicroPython, don't care about these imports, just used for typing
    pass

# TODO: Replace namedtuples with better interface
Date = namedtuple("Date", ("date", "weekday", "month", "year"))
Time = namedtuple("Time", ("seconds", "minutes", "hours"))
DateTime = namedtuple(
    "DateTime", ("date", "weekday", "month", "year", "seconds", "minutes", "hours")
)


class RTC8563:
    address = 0x51
    is_enabled = False

    def __init__(self, i2c: Optional[Union[I2C, SoftI2C]] = None):
        self.is_enabled = False
        if i2c is not None:
            self.enable(i2c)

    def enable(self, i2c: Union[I2C, SoftI2C] = None):
        self._i2c = i2c

        # The built-in RTC sometimes failed to initialize, so first run blankly
        self._i2c.writeto_mem(self.address, 0x00, b"\x00")
        self._i2c.writeto_mem(self.address, 0x00, b"\x00")
        self._i2c.writeto_mem(self.address, 0x0E, b"\x03")

        self.is_enabled = True

    def get_volt_low(self) -> bool:
        return (self._i2c.readfrom_mem(self.address, 0x02, 1)[0] & 0x80) > 0

    @staticmethod
    def bcd2_to_byte(value):
        return ((value >> 4) * 10) + (value & 0x0F)

    @staticmethod
    def byte_to_bcd2(value):
        bcdhigh = value // 10
        return (bcdhigh << 4) | (value - (bcdhigh * 10))

    def get_datetime(self) -> DateTime:
        if not self.is_enabled:
            raise RuntimeError("Device not enabled")

        buf = self._i2c.readfrom_mem(self.address, 0x02, 7)
        return DateTime(
            seconds=self.bcd2_to_byte(buf[0] & 0x7F),
            minutes=self.bcd2_to_byte(buf[1] & 0x7F),
            hours=self.bcd2_to_byte(buf[2] & 0x3F),
            date=self.bcd2_to_byte(buf[3] & 0x3F),
            weekday=self.bcd2_to_byte(buf[4] & 0x07),
            month=self.bcd2_to_byte(buf[5] & 0x1F),
            year=self.bcd2_to_byte(buf[6] & 0xFF) + (1900 if (0x80 & buf[5]) else 2000),
        )

    def get_time(self) -> Time:
        if not self.is_enabled:
            raise RuntimeError("Device not enabled")

        buf = self._i2c.readfrom_mem(self.address, 0x02, 3)
        return Time(
            seconds=self.bcd2_to_byte(buf[0] & 0x7F),
            minutes=self.bcd2_to_byte(buf[1] & 0x7F),
            hours=self.bcd2_to_byte(buf[2] & 0x3F),
        )

    def get_date(self) -> Date:
        if not self.is_enabled:
            raise RuntimeError("Device not enabled")

        buf = self._i2c.readfrom_mem(self.address, 0x05, 4)
        return Date(
            date=self.bcd2_to_byte(buf[0] & 0x3F),
            weekday=self.bcd2_to_byte(buf[1] & 0x07),
            month=self.bcd2_to_byte(buf[2] & 0x1F),
            year=self.bcd2_to_byte(buf[3] & 0xFF) + (1900 if (0x80 & buf[2]) else 2000),
        )

    def set_time(self, time: Time):
        time_buf = bytes(self.byte_to_bcd2(b) for b in (time.seconds, time.minutes, time.hours))
        self._i2c.writeto_mem(self.address, 0x02, time_buf)

    def set_date(self, date: Date):
        weekday = date.weekday
        if weekday > 6 and date.year >= 1900 and (date.mont - 1) < 12:
            year = date.year
            month = date.month
            day = date.date
            if month < 3:
                year -= 1
                month += 12

            y_div_100 = year // 100
            weekday = (
                year + (year >> 2) - y_div_100 + (y_div_100 >> 2) + (13 * month + 8) / 5 + day
            ) % 7

        date_buf = bytes(
            [
                self.byte_to_bcd2(date.date),
                weekday,
                self.byte_to_bcd2(date.month) + (0x80 if date.year < 2000 else 0),
                self.byte_to_bcd2(date.year % 100),
            ]
        )
        self._i2c.writeto_mem(self.address, 0x05, date_buf)

    def set_datetime(self, datetime: DateTime):
        self.set_date(Date(datetime.date, datetime.weekday, datetime.month, datetime.year))
        self.set_time(Time(datetime.seconds, datetime.minutes, datetime.hours))

    # TODO: IRQ management


# #include "RTC8563_Class.hpp"

# #include <stdlib.h>

# namespace m5
# {
#   tm rtc_datetime_t::get_tm(void) const
#   {
#     tm t_st = {
#       time.seconds,
#       time.minutes,
#       time.hours,
#       date.date,
#       date.month - 1,
#       date.year - 1900,
#       date.weekDay,
#       0,
#       0,
#     };
#     return t_st;
#   }

#   void rtc_datetime_t::set_tm(tm& datetime)
#   {
#     date = rtc_date_t { datetime };
#     time = rtc_time_t { datetime };
#   }


#   int RTC8563_Class::setAlarmIRQ(int afterSeconds)
#   {
#     std::uint8_t reg_value = readRegister8(0x01) & ~0x0C;

#     if (afterSeconds < 0)
#     { // disable timer
#       writeRegister8(0x01, reg_value & ~0x01);
#       writeRegister8(0x0E, 0x03);
#       return -1;
#     }

#     std::size_t div = 1;
#     std::uint8_t type_value = 0x82;
#     if (afterSeconds < 270)
#     {
#       if (afterSeconds > 255) { afterSeconds = 255; }
#     }
#     else
#     {
#       div = 60;
#       afterSeconds = (afterSeconds + 30) / div;
#       if (afterSeconds > 255) { afterSeconds = 255; }
#       type_value = 0x83;
#     }

#     writeRegister8(0x0E, type_value);
#     writeRegister8(0x0F, afterSeconds);

#     writeRegister8(0x01, (reg_value | 0x01) & ~0x80);
#     return afterSeconds * div;
#   }

#   int RTC8563_Class::setAlarmIRQ(const rtc_time_t &time)
#   {
#     union
#     {
#       std::uint32_t raw = ~0;
#       std::uint8_t buf[4];
#     };
#     bool irq_enable = false;

#     if (time.minutes >= 0)
#     {
#       irq_enable = true;
#       buf[0] = byteToBcd2(time.minutes) & 0x7f;
#     }

#     if (time.hours >= 0)
#     {
#       irq_enable = true;
#       buf[1] = byteToBcd2(time.hours) & 0x3f;
#     }

#     writeRegister(0x09, buf, 4);

#     if (irq_enable)
#     {
#       bitOn(0x01, 0x02);
#     } else {
#       bitOff(0x01, 0x02);
#     }

#     return irq_enable;
#   }

#   int RTC8563_Class::setAlarmIRQ(const rtc_date_t &date, const rtc_time_t &time)
#   {
#     union
#     {
#       std::uint32_t raw = ~0;
#       std::uint8_t buf[4];
#     };
#     bool irq_enable = false;

#     if (time.minutes >= 0)
#     {
#       irq_enable = true;
#       buf[0] = byteToBcd2(time.minutes) & 0x7f;
#     }

#     if (time.hours >= 0)
#     {
#       irq_enable = true;
#       buf[1] = byteToBcd2(time.hours) & 0x3f;
#     }

#     if (date.date >= 0)
#     {
#       irq_enable = true;
#       buf[2] = byteToBcd2(date.date) & 0x3f;
#     }

#     if (date.weekDay >= 0)
#     {
#       irq_enable = true;
#       buf[3] = byteToBcd2(date.weekDay) & 0x07;
#     }

#     writeRegister(0x09, buf, 4);

#     if (irq_enable)
#     {
#       bitOn(0x01, 0x02);
#     }
#     else
#     {
#       bitOff(0x01, 0x02);
#     }

#     return irq_enable;
#   }

#   bool RTC8563_Class::getIRQstatus(void)
#   {
#     return _init && (0x0C & readRegister8(0x01));
#   }

#   void RTC8563_Class::clearIRQ(void)
#   {
#     if (!_init) { return; }
#     bitOff(0x01, 0x0C);
#   }

#   void RTC8563_Class::disableIRQ(void)
#   {
#     if (!_init) { return; }
#     // disable alerm (bit7:1=disabled)
#     static constexpr const std::uint8_t buf[4] = { 0x80, 0x80, 0x80, 0x80 };
#     writeRegister(0x09, buf, 4);

#     // disable timer (bit7:0=disabled)
#     writeRegister8(0x0E, 0);

#     // clear flag and INT enable bits
#     writeRegister8(0x01, 0x00);
#   }

#   void RTC8563_Class::setSystemTimeFromRtc(struct timezone* tz)
#   {
# #if !defined (M5UNIFIED_PC_BUILD)
#     rtc_datetime_t dt;
#     if (getDateTime(&dt))
#     {
#       tm t_st;
#       t_st.tm_isdst = -1;
#       t_st.tm_year = dt.date.year - 1900;
#       t_st.tm_mon  = dt.date.month - 1;
#       t_st.tm_mday = dt.date.date;
#       t_st.tm_hour = dt.time.hours;
#       t_st.tm_min  = dt.time.minutes;
#       t_st.tm_sec  = dt.time.seconds;
#       timeval now;
#       // mktime(3) uses localtime, force UTC
#       char *oldtz = getenv("TZ");
#       setenv("TZ", "GMT0", 1);
#       tzset(); // Workaround for https://github.com/espressif/esp-idf/issues/11455
#       now.tv_sec = mktime(&t_st);
#       if (oldtz)
#       {
#         setenv("TZ", oldtz, 1);
#       } else {
#         unsetenv("TZ");
#       }
#       now.tv_usec = 0;
#       settimeofday(&now, tz);
#     }
# #endif
#   }
# }
