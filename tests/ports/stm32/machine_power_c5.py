# Exercise the clock-preserving sleep mode and reject unsupported C5 controls.
import machine
import sys

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

freq = machine.freq()
assert freq[0] == 144000000, freq
assert freq[1] == 144000000, freq
assert freq[2] == 144000000, freq
assert freq[3] == 144000000, freq

machine.lightsleep()
assert machine.freq() == freq

try:
    machine.freq(100000000)
except NotImplementedError:
    pass
else:
    raise AssertionError("clock change must be rejected")

try:
    machine.lightsleep(1000)
except NotImplementedError:
    pass
else:
    raise AssertionError("timed lightsleep must be rejected")

try:
    machine.deepsleep()
except NotImplementedError:
    pass
else:
    raise AssertionError("deepsleep must be rejected")

print("C5 clock query and interrupt sleep passed")
