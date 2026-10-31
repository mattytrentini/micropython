# Exercise C5 clock transitions and RTC-timed STOP sleep.
import errno
import machine
import sys
import time

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

weact = "WeAct Studio STM32C552" in sys.implementation._machine
# The 8 MHz HSE is below the USB FSDEV driver's APB clock minimum.
low_clock = 144000000 if weact else 48000000
freq = machine.freq()
assert freq in (
    (low_clock,) * 4,
    (144000000, 144000000, 144000000, 144000000),
), freq

# Clock changes preserve the independent PSI/PSIDIV3 USB clock. Active
# peripherals may reject the transition; in that case no clock may change.
try:
    machine.freq(low_clock)
except OSError as exc:
    assert exc.errno == errno.EBUSY, exc
    assert machine.freq() == freq
else:
    assert machine.freq() == (low_clock,) * 4
    machine.freq(144000000)
    assert machine.freq() == (144000000, 144000000, 144000000, 144000000)

unsupported = (100000000, 48000000, 8000000) if weact else (100000000,)
before_unsupported = machine.freq()
if weact:
    # Reject 8 MHz even without the diagnostic UART's independent busy guard.
    machine.UART(1).deinit()
try:
    for clock in unsupported:
        try:
            machine.freq(clock)
        except ValueError:
            pass
        else:
            raise AssertionError("unsupported clock must be rejected")
        assert machine.freq() == before_unsupported
finally:
    if weact:
        machine.UART(1, 115200)

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
