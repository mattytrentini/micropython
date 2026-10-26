from pyb import RTC
import time

rtc = RTC()
old_datetime = rtc.datetime()
set_datetime = (2024, 2, 29, 4, 12, 34, 30, 0)
def datetime_matches():
    got = rtc.datetime()
    wall = time.localtime()
    wall_date_time = wall[:5]
    expected_date_time = set_datetime[:3] + set_datetime[4:6]
    second_ok = got[6] in (30, 31) and wall[5] in (30, 31)
    return got[:6] == set_datetime[:6] and wall_date_time == expected_date_time and second_ok

try:
    rtc.datetime(set_datetime)
    print(datetime_matches())
    print(time.time_ns() > 0)

    # Verify the RTC wakeup timer dispatches a deferred callback.
    wakeup_events = []
    rtc.wakeup(2, 10, lambda line: wakeup_events.append(line))
    try:
        time.sleep_ms(50)
    finally:
        rtc.wakeup(None)
    print(bool(wakeup_events))
    # Reinitializing the Python object must not reset an initialized calendar.
    rtc.init()
    print(datetime_matches())
finally:
    rtc.datetime(old_datetime)


