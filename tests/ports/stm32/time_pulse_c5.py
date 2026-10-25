# C5 pulse measurement against a local timer-generated PWM signal.
import sys
import machine
from machine import Pin
from pyb import Timer

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

pin = Pin.cpu.A5
timer = Timer(2, freq=1000)
try:
    timer.channel(1, Timer.PWM, pin=pin, pulse_width_percent=30)
    # The first edge may occur mid-cycle while the timer channel is initialized.
    machine.time_pulse_us(pin, 1, 10_000)
    widths = [machine.time_pulse_us(pin, 1, 10_000) for _ in range(5)]
    assert all(240 <= width <= 360 for width in widths), widths
finally:
    timer.deinit()

static = pin
static.init(Pin.OUT, value=0)
assert machine.time_pulse_us(static, 1, 2_000) == -2
static.value(1)
assert machine.time_pulse_us(static, 1, 2_000) == -1
static.init(Pin.ANALOG)

print("C5 pulse timing and timeout states")
