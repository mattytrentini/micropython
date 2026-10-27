# Verify the C5 hardware RNG returns changing machine.rng() values.
import sys
import machine

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

values = [machine.rng() for _ in range(8)]
assert all(0 <= value <= 0x3FFFFFFF for value in values), values
assert len(set(values)) > 1, values

print("C5 machine.rng returns changing values")
