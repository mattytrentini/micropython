# Fixture opt-in is mandatory: PA4 is a battery input on some C5 boards.
# On NUCLEO-C5A3ZG or WeAct C552 with PA4 disconnected, provide
# machine_dac_c5_fixture.py containing PA4_UNCONNECTED = True.
# No external wiring is required.
import sys
import time

try:
    from machine import DAC, Pin, mem32
    import machine_dac_c5_fixture as fixture
except ImportError:
    print("SKIP")
    raise SystemExit

if (
    not any(
        board in sys.implementation._machine
        for board in ("NUCLEO_C5A3ZG", "WeAct Studio STM32C552")
    )
    or not fixture.PA4_UNCONNECTED
):
    print("SKIP")
    raise SystemExit

DAC1 = 0x42028400


def rejects(exception, function):
    try:
        function()
    except exception:
        return
    raise AssertionError("invalid DAC operation accepted")


# Invalid resource selection must not configure any pad.
rejects(ValueError, lambda: DAC(0))
rejects(ValueError, lambda: DAC(2))
rejects(ValueError, lambda: DAC(Pin.cpu.A5))
rejects(TypeError, lambda: DAC(1, bits=8))

dac = DAC(Pin.cpu.A4)
try:
    assert mem32[DAC1 + 0x2C] & 4095 == 0
    for value in (0, 1, 255, 256, 2048, 4095):
        dac.write(value)
        time.sleep_us(20)
        assert mem32[DAC1 + 0x2C] & 4095 == value
        assert DAC(1) is dac
        assert mem32[DAC1 + 0x2C] & 4095 == value
    for value in (-1, 4096, 65536):
        rejects(ValueError, lambda: dac.write(value))
        assert mem32[DAC1 + 0x2C] & 4095 == 4095
    rejects(TypeError, lambda: dac.write(None))

    # Same-pad ADC sensing catches a stalled analog core that DOR alone cannot.
    from machine import ADC

    adc = ADC(Pin.cpu.A4)
    try:
        for value in (256, 1024, 2048, 3072, 3840):
            dac.write(value)
            time.sleep_ms(2)
            for _ in range(3):
                assert abs((adc.read_u16() >> 4) - value) < 128
    finally:
        adc.deinit()

    dac.deinit()
    dac.deinit()
    rejects(OSError, lambda: dac.write(123))
    assert DAC(1) is dac
    time.sleep_us(20)
    assert mem32[DAC1 + 0x2C] & 4095 == 0
    dac.write(1234)
    time.sleep_us(20)
    assert mem32[DAC1 + 0x2C] & 4095 == 1234
    print("C5 machine.DAC bounds, output and lifecycle PASS")
finally:
    dac.deinit()
