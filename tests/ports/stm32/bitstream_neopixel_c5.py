# Verify that the STM32C5 bitstream API drives the frozen NeoPixel driver.
import sys
from machine import Pin

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

from neopixel import NeoPixel

np = NeoPixel(Pin.cpu.A5, 1)
np[0] = (0x12, 0x34, 0x56)
assert np.buf == bytes((0x34, 0x12, 0x56))
assert np[0] == (0x12, 0x34, 0x56)
np.write()
assert np.pin.value() == 0
np.fill((0xA5, 0x5A, 0xC3))
assert np.buf == bytes((0x5A, 0xA5, 0xC3))
np.write()
assert np.pin.value() == 0

print("C5 machine.bitstream and NeoPixel write")
