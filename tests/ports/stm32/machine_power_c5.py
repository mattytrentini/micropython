# Exercise C5 clock transitions and RTC-timed STOP sleep.
import errno
import machine
import sys
import time

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

freq = machine.freq()
assert freq in ((48000000, 48000000, 48000000, 48000000),
                (144000000, 144000000, 144000000, 144000000)), freq

# Clock changes preserve the independent PSI/PSIDIV3 USB clock. Active
# peripherals may reject the transition; in that case no clock may change.
try:
    machine.freq(48000000)
except OSError as exc:
    assert exc.errno == errno.EBUSY, exc
    assert machine.freq() == freq
else:
    assert machine.freq() == (48000000, 48000000, 48000000, 48000000)
    machine.freq(144000000)
    assert machine.freq() == (144000000, 144000000, 144000000, 144000000)

try:
    machine.freq(100000000)
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
