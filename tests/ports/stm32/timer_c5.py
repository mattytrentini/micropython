# No external wiring: use the Nucleo LED pad and passive pulls on unused pads.
import gc
import sys
import time
from machine import Pin, time_pulse_us
from pyb import Timer

if "NUCLEO_C5A3ZG" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

hits = [0, 0]


def tick(timer):
    hits[0] += 1


# Exercise each physical counter and update vector, including basic timers.
for ident in (1, 2, 3, 4, 5, 6, 7, 8, 12, 15, 16, 17):
    hits[0] = 0
    timer = Timer(ident, freq=100, callback=tick)
    try:
        time.sleep_ms(45)
        timer.callback(None)
        assert 3 <= hits[0] <= 6
        previous = hits[0]
        time.sleep_ms(15)
        assert hits[0] == previous
    finally:
        timer.deinit()
    previous = timer.counter()
    time.sleep_ms(2)
    assert timer.counter() == previous
print("C5 update callbacks OK")

# TIM3/4 are also 32-bit on C5; preserve unsigned register values at the boundary.
for ident in (2, 3, 4, 5):
    timer = Timer(ident, freq=1000)
    try:
        timer.period(0xFFFFFFFF)
        time.sleep_ms(2)
        assert timer.period() == 0xFFFFFFFF
        assert 0 < timer.freq() < 1
        channel = timer.channel(1, Timer.OC_TIMING)
        channel.compare(0x87654321)
        assert channel.compare() == 0x87654321
    finally:
        timer.deinit()
    timer.counter(0xF1234567)
    assert timer.counter() == 0xF1234567
print("C5 unsigned counters OK")

# Replacing a non-head channel must retain the others and their interrupts.
def sibling(timer):
    hits[1] += 1


timer = Timer(3, freq=100)
try:
    first = timer.channel(1, Timer.PWM, pulse_width_percent=25, callback=tick)
    timer.channel(2, Timer.PWM)
    third = timer.channel(3, Timer.PWM, pulse_width_percent=50, callback=sibling)
    replacement = timer.channel(2, Timer.PWM)
    assert timer.channel(1) is first
    assert timer.channel(3) is third
    assert timer.channel(2) is replacement
    hits[:] = [0, 0]
    gc.collect()
    time.sleep_ms(45)
    assert hits[0] >= 3 and hits[1] >= 3
finally:
    timer.deinit()
print("C5 channel ownership OK")

# Preserve the pulse-width API added after the original C5 implementation.
timer = Timer(2, freq=1000)
try:
    channel = timer.channel(1, Timer.PWM, pulse_width_us=250)
    assert channel.pulse_width_us() == 250
    assert channel.pulse_width_ns() == 250000
    channel.pulse_width_us(125)
    assert channel.pulse_width_us() == 125
    channel.pulse_width_ns(375000)
    assert channel.pulse_width_ns() == 375000
    channel = timer.channel(1, Timer.PWM, pulse_width_ns=500000)
    assert channel.pulse_width_us() == 500
    channel = timer.channel(1, Timer.OC_TIMING, compare=12345, polarity=Timer.LOW)
    assert channel.compare() == 12345
finally:
    timer.deinit()
print("C5 pulse-width units OK")

# Reject invalid requests before replacing a channel that is already running.
timer = Timer(2, freq=100)
try:
    channel = timer.channel(1, Timer.PWM)
    for kwargs in (
        {"mode": -1},
        {"mode": Timer.IC, "polarity": 123},
        {"mode": Timer.IC, "callback": 123},
        {"mode": Timer.ENC_AB, "callback": tick},
    ):
        try:
            timer.channel(1, **kwargs)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid channel configuration accepted")
        assert timer.channel(1) is channel
finally:
    timer.deinit()
for ident, kwargs in (
    (6, {"freq": 100, "mode": Timer.DOWN}),
    (2, {"prescaler": 65536, "period": 100}),
    (1, {"prescaler": 0, "period": 65536}),
):
    timer = Timer(ident)
    try:
        timer.init(**kwargs)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid timer configuration accepted")
    finally:
        timer.deinit()
print("C5 configuration validation OK")

# The same LED pad exposes general, advanced-main and advanced-complementary PWM.
pin = Pin.cpu.A5
for ident, number, mode, expected in (
    (2, 1, Timer.PWM, 2500),
    (2, 1, Timer.PWM_INVERTED, 7500),
    (1, 3, Timer.PWM, 2500),
    (8, 1, Timer.PWM, 7500),
):
    timer = Timer(ident, freq=100)
    try:
        channel = timer.channel(number, mode, pin=pin, pulse_width_percent=25, callback=tick)
        hits[0] = 0
        time.sleep_ms(35)
        assert hits[0] >= 2
        channel.callback(None)
        # Synchronize first: time_pulse_us otherwise measures a partial active pulse.
        assert time_pulse_us(pin, 0, 30000) > 0
        width = time_pulse_us(pin, 1, 30000)
        assert abs(width - expected) < 200
        if ident == 2 and mode == Timer.PWM:
            try:
                timer.channel(1, Timer.PWM, pin=Pin.cpu.A1)
            except ValueError:
                pass
            else:
                raise AssertionError("wrong channel pin accepted")
            assert timer.channel(1) is channel
            channel.pulse_width_percent(0)
            time.sleep_ms(15)
            assert pin.value() == 0
            channel.pulse_width_percent(100)
            time.sleep_ms(15)
            assert pin.value() == 1
    finally:
        timer.deinit()
        pin.init(Pin.OUT, value=0)
print("C5 physical PWM OK")

# True capture edges come from the pad, not software-generated timer events.
pin = Pin.cpu.A0
for polarity, expected in ((Timer.RISING, 5), (Timer.FALLING, 5), (Timer.BOTH, 10)):
    timer = Timer(2)
    timer.init(prescaler=timer.source_freq() // 1000000 - 1, period=59999)
    captures = [0] * 12

    def capture(timer):
        index = hits[0]
        if index < len(captures):
            captures[index] = channel.capture()
        hits[0] = index + 1

    try:
        channel = timer.channel(1, Timer.IC, pin=pin, polarity=polarity, callback=capture)
        pin.init(Pin.ALT, pull=Pin.PULL_DOWN, alt=1)
        time.sleep_ms(2)
        hits[0] = 0
        for edge in range(10):
            pin.init(Pin.ALT, pull=Pin.PULL_UP if edge % 2 == 0 else Pin.PULL_DOWN, alt=1)
            time.sleep_ms(2)
        assert hits[0] == expected
        assert all(0 <= value <= 59999 for value in captures[:expected])
        for previous, current in zip(captures[:expected - 1], captures[1:expected]):
            elapsed = (current - previous) % 60000
            assert (1500 <= elapsed <= 3500) if polarity == Timer.BOTH else (3000 <= elapsed <= 5500)
    finally:
        timer.deinit()
        pin.init(Pin.ANALOG)
print("C5 physical capture OK")


def pull(pin, level):
    pin.init(Pin.ALT, pull=Pin.PULL_UP if level else Pin.PULL_DOWN, alt=1)
    time.sleep_us(20)


first_pin, second_pin = Pin.cpu.A0, Pin.cpu.B3
for mode in (Timer.ENC_A, Timer.ENC_B, Timer.ENC_AB):
    timer = Timer(2, prescaler=0, period=65535)
    try:
        timer.channel(1, mode, pin=first_pin)
        pull(first_pin, 0)
        pull(second_pin, 0)
        timer.counter(100)
        pull(first_pin, 1)
        first_count = timer.counter()
        assert (first_count != 100) == (mode != Timer.ENC_B)
        pull(second_pin, 1)
        assert (timer.counter() != first_count) == (mode != Timer.ENC_A)
        pull(first_pin, 0)
        pull(second_pin, 0)
        assert abs(timer.counter() - 100) == (4 if mode == Timer.ENC_AB else 2)
        pull(second_pin, 1)
        pull(first_pin, 1)
        pull(second_pin, 0)
        pull(first_pin, 0)
        assert timer.counter() == 100
    finally:
        timer.deinit()
        first_pin.init(Pin.ANALOG)
        second_pin.init(Pin.ANALOG)
print("C5 physical encoders OK")

soft_events = []


def allocating(timer):
    soft_events.append(bytearray(17))


timer = Timer(16, freq=100, callback=allocating, hard=False)
try:
    gc.collect()
    time.sleep_ms(55)
finally:
    timer.deinit()
assert 3 <= len(soft_events) <= 8
print("C5 soft callbacks OK")
