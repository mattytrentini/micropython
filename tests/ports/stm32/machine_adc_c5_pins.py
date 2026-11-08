# Opt-in external input test: never guess board wiring or drive output pins.
# Supply machine_adc_c5_fixture.py on the board with CASES containing tuples:
# (machine.Pin object, min_u16, max_u16).
# The operator must ensure each pin is safe to place in analog mode and held
# at a known, valid input voltage. Include ADC1/ADC2 and C5A3 ADC3 inputs to
# exercise routing and coexistence across peripheral instances.
import sys

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

try:
    from machine_adc_c5_fixture import CASES
except ImportError:
    print("SKIP")
    raise SystemExit

from machine import ADC, mem32

if not CASES:
    print("SKIP")
    raise SystemExit

reference = ADC(ADC.CORE_VREF)
handles = []
try:
    baseline = sum(reference.read_u16() for _ in range(8)) // 8
    for pin, minimum, maximum in CASES:
        assert 0 <= minimum <= maximum <= 65535
        adc = ADC(pin)
        handles.append(adc)
        for _ in range(8):
            assert minimum <= adc.read_u16() <= maximum
            # Initialising ADC2/ADC3 must not reset ADC1 or its internal paths.
            assert abs(reference.read_u16() - baseline) < baseline // 10
    for adc in handles:
        adc.deinit()
        assert abs(reference.read_u16() - baseline) < baseline // 10
finally:
    reference.deinit()
    for adc in handles:
        adc.deinit()
print("C5 machine ADC fixture pin selection and coexistence")
