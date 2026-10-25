from pyb import RTC
import time

rtc = RTC()
old_datetime = rtc.datetime()
set_datetime = (2024, 2, 29, 4, 12, 34, 30, 0)
rtc.datetime(set_datetime)

def datetime_matches():
    got = rtc.datetime()
    wall = time.localtime()
    wall_date_time = wall[:3] + wall[4:6]
    expected_date_time = set_datetime[:3] + set_datetime[4:6]
    second_ok = got[6] in (30, 31) and wall[5] in (30, 31)
    return got[:6] == set_datetime[:6] and wall_date_time == expected_date_time and second_ok

print(datetime_matches())
print(time.time_ns() > 0)

# Reinitializing the Python object must not reset an initialized calendar.
rtc.init()
print(datetime_matches())

rtc.datetime(old_datetime)
