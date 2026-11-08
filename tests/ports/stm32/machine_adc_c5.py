# C5 internal channels only: no fixture, pin changes, or factory voltage assumptions.
import sys

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

from machine import ADC, mem32

for channel in (-1, 14, 19, 255, 258, 65536):
    try:
        ADC(channel)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid channel accepted")

full = ADC(ADC.VREF)
assert full.read_u16() == 65535
full.deinit()
full.deinit()
try:
    full.read_u16()
except OSError:
    pass
else:
    raise AssertionError("deinitialized ADC remained usable")

ref = ADC(ADC.CORE_VREF)
temp = ADC(ADC.CORE_TEMP)
# Numeric ADC1 hardware channels 12 and 13 also select the internal paths.
numbered_ref = ADC(13)
numbered_temp = ADC(12)
try:
    baseline = sum(ref.read_u16() for _ in range(8)) // 8
    assert 0 < baseline < 65535
    for _ in range(8):
        assert 0 < temp.read_u16() < 65535
        assert abs(numbered_ref.read_u16() - baseline) < baseline // 10
        assert 0 < numbered_temp.read_u16() < 65535

    # Deinit must release ADEN, without invalidating other logical handles or
    # clearing the common paths. ADC1 CR/ADC12 CCR addresses are common to
    # the C5A3 and C552 CMSIS definitions.
    paths = mem32[0x42028308] & ((1 << 22) | (1 << 23))
    ref.deinit()
    assert mem32[0x42028008] & 1 == 0
    assert abs(numbered_ref.read_u16() - baseline) < baseline // 10
    assert mem32[0x42028308] & ((1 << 22) | (1 << 23)) == paths
    ref.deinit()  # An already-deinitialized handle must not disable its sibling.
    assert mem32[0x42028008] & 1
    assert 0 < temp.read_u16() < 65535
finally:
    ref.deinit()
    temp.deinit()
    numbered_ref.deinit()
    numbered_temp.deinit()
assert mem32[0x42028008] & 1 == 0
print("C5 machine ADC internal channels and lifecycle")
