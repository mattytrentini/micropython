# Exercise C5 clock transitions and RTC-timed STOP sleep.
import errno
import machine
import sys
import time

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

hse = 8000000 if "WeAct Studio STM32C552" in sys.implementation._machine else 48000000
freq = machine.freq()
assert freq in (
    (hse, hse, hse, hse),
    (144000000, 144000000, 144000000, 144000000),
), freq

# Clock changes preserve the independent PSI/PSIDIV3 USB clock. Active
# peripherals may reject the transition; in that case no clock may change.
try:
    machine.freq(hse)
except OSError as exc:
    assert exc.errno == errno.EBUSY, exc
    assert machine.freq() == freq
else:
    assert machine.freq() == (hse, hse, hse, hse)
    machine.freq(144000000)
    assert machine.freq() == (144000000, 144000000, 144000000, 144000000)

unsupported = (100000000, 48000000) if hse == 8000000 else (100000000,)
for clock in unsupported:
    try:
        machine.freq(clock)
    except ValueError:
        pass
    else:
        raise AssertionError("unsupported clock must be rejected")

# An active USB host that has not suspended the bus cannot safely enter STOP.
# Otherwise the RTC wake timer must return to the running clock domain.
before = time.time_ns()
try:
    machine.lightsleep(30)
except OSError as exc:
    assert exc.errno == errno.EBUSY, exc
else:
    assert time.time_ns() > before

print("C5 clock transition and timed sleep path passed")
