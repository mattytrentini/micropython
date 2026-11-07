# No jumper: time_pulse_us reads the PWM output pad itself.
# Run over USB on a freshly reset board, with PA0, PA1, PA10 and PE2 expansion
# pads UNCONNECTED (including any UART adapter on PA10). Do not run with other
# timer users. The advanced test deinitializes UART1 on XIAO/WeAct, then restores
# its board-default 115200-baud configuration in finally. Use only the USB REPL.
import sys
import time

try:
    from machine import Pin, PWM, UART, time_pulse_us
except ImportError:
    print("SKIP")
    raise SystemExit

board = sys.implementation._machine
if not any(name in board for name in ("XIAO STM32C5", "NUCLEO_C5A3ZG", "WeAct Studio STM32C552")):
    print("SKIP")
    raise SystemExit

# Both C5 AF tables map PA0/PA1 to TIM2_CH1/2 then TIM5_CH1/2; machine.PWM
# selects the second main AF, so these share the 32-bit TIM5. PA10 has only
# TIM1_CH3: an advanced 16-bit output requiring MOE. PB6 is NOT a common TIM16
# main output (it has TIM16_CH1N), so it is deliberately not used here.
first_pin, second_pin, advanced_pin = Pin.cpu.A0, Pin.cpu.A1, Pin.cpu.A10
# PE2 has TIM3_ETR or LPTIM1_IN2, but no TIMx_CHy output in either AF table.
unsupported_pin = Pin.cpu.E2
outputs = []
diagnostic_uart = None


def pwm(pin, **kwargs):
    obj = PWM(pin, **kwargs)
    outputs.append(obj)
    return obj


def settle(freq):
    # CCR/ARR are preloaded. Allow two new periods (also one old period for
    # the 100 -> 200 Hz transition below) before observing the pad.
    time.sleep_us(2000000 // freq + 100)


def waveform(pin, freq, high_us):
    period = 1000000 // freq
    settle(freq)
    timeout = 3 * period
    for level, expected in ((0, period - high_us), (1, high_us)):
        for _ in range(3):
            # Synchronize on the opposite pulse, never measure a partial pulse.
            assert time_pulse_us(pin, 1 - level, timeout) > 0
            width = time_pulse_us(pin, level, timeout)
            # C-loop sampling takes a few us; allow interrupt/USB jitter too.
            assert abs(width - expected) <= 40, (level, width, expected)


def constant(pin, freq, level):
    settle(freq)
    assert pin.value() == level
    # Check that this is a constant level, not just a lucky sample of PWM.
    assert time_pulse_us(pin, 0, 2000000 // freq) < 0
    assert time_pulse_us(pin, 1, 2000000 // freq) < 0
    assert pin.value() == level


def reported_waveform(pin, obj):
    # Check readback against the pad without prescribing an incidental default.
    duty = obj.duty_u16()
    freq = obj.freq()
    assert 0 <= duty <= 65535
    if duty in (0, 65535):
        constant(pin, freq, duty == 65535)
    else:
        waveform(pin, freq, (1000000 * duty + 65535 * freq // 2) // (65535 * freq))


try:
    first = pwm(first_pin, freq=100, duty_u16=32768, invert=False)
    assert first.freq() == 100
    assert abs(first.duty_u16() - 32768) <= 2
    waveform(first_pin, 100, 5000)
    for duty, level in ((0, 0), (65535, 1)):
        first.duty_u16(duty)
        assert first.duty_u16() == duty
        constant(first_pin, 100, level)
    first.duty_u16(32768)
    waveform(first_pin, 100, 5000)
    print("C5 PWM u16 endpoints OK")

    first.duty_ns(2500000)
    assert abs(first.duty_ns() - 2500000) <= 100
    waveform(first_pin, 100, 2500)
    first.init(invert=True)
    waveform(first_pin, 100, 7500)
    for duty, level in ((0, 1), (65535, 0)):
        first.duty_u16(duty)
        constant(first_pin, 100, level)
    first.init(invert=False, duty_u16=32768)
    waveform(first_pin, 100, 5000)
    print("C5 PWM nanoseconds and inversion OK")

    second = pwm(second_pin, freq=100, duty_ns=1250000, invert=False)
    for freq in (100, 200, 100):
        first.freq(freq)
        assert first.freq() == freq and second.freq() == freq
        assert abs(first.duty_u16() - 32768) <= 2
        assert abs(second.duty_ns() - 1250000) <= 100
        waveform(first_pin, freq, 500000 // freq)
        waveform(second_pin, freq, 1250)
    print("C5 PWM shared frequency preserves duties OK")

    for invalid in (0, -1, 1000000000):
        try:
            first.freq(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid PWM frequency accepted")
        assert first.freq() == 100 and second.freq() == 100
        assert abs(first.duty_u16() - 32768) <= 2
        assert abs(second.duty_ns() - 1250000) <= 100
        waveform(first_pin, 100, 5000)
        waveform(second_pin, 100, 1250)
    try:
        unexpected = PWM(unsupported_pin, freq=100, duty_u16=32768)
    except ValueError:
        pass
    else:
        unexpected.deinit()
        raise AssertionError("non-PWM pad accepted")
    print("C5 PWM rejected requests preserve outputs OK")

    first.deinit()
    # Keep AF2/TIM5 connected; a weak pull defines the disabled pad's level.
    first_pin.init(Pin.ALT, pull=Pin.PULL_DOWN, alt=2)
    second.freq(200)
    waveform(second_pin, 200, 1250)
    constant(first_pin, 200, 0)
    second.freq(100)
    waveform(second_pin, 100, 1250)
    second.duty_ns(2500000)
    waveform(second_pin, 100, 2500)
    second.deinit()
    first = pwm(first_pin, freq=200, duty_u16=16384, invert=False)
    waveform(first_pin, 200, 1250)
    first.deinit()
    second = pwm(second_pin, freq=100, duty_ns=1250000, invert=False)
    waveform(second_pin, 100, 1250)
    second.deinit()
    print("C5 PWM channel deinit and recreation OK")

    first = pwm(first_pin, freq=100, duty_u16=32768)
    replacement = pwm(first_pin, freq=200)
    reported_waveform(first_pin, replacement)
    replacement.deinit()
    print("C5 PWM constructor readback matches output OK")

    first = pwm(first_pin, freq=1, duty_u16=32768)
    assert first.freq() == 1
    assert time_pulse_us(first_pin, 0, 3000000) > 0
    high = time_pulse_us(first_pin, 1, 3000000)
    low = time_pulse_us(first_pin, 0, 3000000)
    assert abs(high - 500000) <= 200, high
    assert abs(low - 500000) <= 200, low
    first.deinit()
    print("C5 PWM 32-bit low frequency OK")

    # A duty configured before any frequency must start with its sibling's period.
    second = pwm(second_pin, duty_u16=32768)
    first = pwm(first_pin, freq=100)
    waveform(second_pin, 100, 5000)
    first.duty_u16(32768)
    second.deinit()
    second_pin.init(Pin.ALT, pull=Pin.PULL_DOWN, alt=2)
    first.freq(200)
    constant(second_pin, 200, 0)
    assert abs(second.duty_u16() - 32768) <= 2
    assert abs(second.duty_ns() - 2500000) <= 100
    # Restart the cached ratio against the new period, not the stale compare.
    second.init()
    waveform(second_pin, 200, 2500)
    second.deinit()
    first.freq(100)
    second.freq(100)
    waveform(second_pin, 100, 5000)
    first.deinit()
    second.deinit()

    second = pwm(second_pin, duty_ns=1250000)
    first = pwm(first_pin, freq=100)
    waveform(second_pin, 100, 1250)
    first.duty_u16(32768)
    second.deinit()
    second_pin.init(Pin.ALT, pull=Pin.PULL_DOWN, alt=2)
    first.freq(200)
    constant(second_pin, 200, 0)
    assert abs(second.duty_ns() - 1250000) <= 100
    second.init()
    waveform(second_pin, 200, 1250)
    first.deinit()
    second.deinit()
    print("C5 PWM pending duties and cached restarts OK")

    if "NUCLEO_C5A3ZG" not in board:
        diagnostic_uart = UART(1)
        diagnostic_uart.deinit()
    advanced = pwm(advanced_pin, freq=100, duty_u16=16384, invert=True)
    assert advanced.freq() == 100
    assert abs(advanced.duty_u16() - 16384) <= 4
    waveform(advanced_pin, 100, 7500)
    advanced.init(invert=False, duty_ns=1250000)
    waveform(advanced_pin, 100, 1250)
    advanced.freq(200)
    assert advanced.freq() == 200
    assert abs(advanced.duty_ns() - 1250000) <= 100
    waveform(advanced_pin, 200, 1250)
    advanced.deinit()
    advanced = pwm(advanced_pin, freq=100, duty_u16=32768, invert=False)
    waveform(advanced_pin, 100, 5000)
    advanced.deinit()
    advanced.init(freq=100, duty_u16=32768, invert=False)
    waveform(advanced_pin, 100, 5000)
    advanced.deinit()
    advanced.duty_u16(32768)
    waveform(advanced_pin, 100, 5000)
    print("C5 PWM advanced timer OK")
finally:
    for obj in outputs:
        obj.deinit()
    first_pin.init(Pin.ANALOG)
    second_pin.init(Pin.ANALOG)
    unsupported_pin.init(Pin.ANALOG)
    if "NUCLEO_C5A3ZG" in board or diagnostic_uart is not None:
        advanced_pin.init(Pin.ANALOG)
    if diagnostic_uart is not None:
        diagnostic_uart.init(115200, bits=8, parity=None, stop=1)
