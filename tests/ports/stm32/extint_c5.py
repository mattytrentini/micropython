# Exercise C5 LED-pad edges and independent pending lines on the Nucleo, without wiring.
import gc
import sys
import time
from machine import Pin, disable_irq, enable_irq
from pyb import ExtInt

board = sys.implementation._machine
if "NUCLEO_C5A3ZG" not in board:
    print("SKIP")
    raise SystemExit
pin, peer, line = Pin.cpu.A5, Pin.cpu.C5, 5

original = pin.value()
events = [0, None]
legacy_events = [0, 0]
extints = []


def wait_count(counts, index, expected):
    deadline = time.ticks_add(time.ticks_ms(), 200)
    while counts[index] < expected:
        assert time.ticks_diff(deadline, time.ticks_ms()) > 0, "EXTI callback timeout"
        time.sleep_ms(1)
    assert counts[index] == expected


def hard_edge(arg):
    events[0] += 1
    events[1] = arg


def install_soft():
    # Only the port's callback root keeps this function alive after return.
    def callback(arg):
        events[0] += 1
        events[1] = [arg]  # Allocation is only allowed in a scheduled callback.

    pin.irq(handler=callback, trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING)


try:
    pin.init(Pin.OUT, value=0)
    pin.irq(handler=hard_edge, trigger=Pin.IRQ_RISING, hard=True)
    pin(1)
    wait_count(events, 0, 1)
    assert events[1] is pin
    pin(0)
    time.sleep_ms(2)
    assert events[0] == 1

    events[0] = 0
    pin.irq(handler=hard_edge, trigger=Pin.IRQ_FALLING, hard=True)
    pin(1)
    time.sleep_ms(2)
    assert events[0] == 0
    pin(0)
    wait_count(events, 0, 1)

    try:
        peer.irq(handler=hard_edge, trigger=Pin.IRQ_RISING)
    except OSError:
        pass
    else:
        raise AssertionError("different ports cannot own the same EXTI line")
    # A rejected registration must leave the original owner working.
    pin(1)
    pin(0)
    wait_count(events, 0, 2)

    try:
        ExtInt(pin, ExtInt.IRQ_RISING, Pin.PULL_NONE, legacy_events.append)
    except ValueError:
        pass
    else:
        raise AssertionError("ExtInt cannot take a line owned by Pin.irq")

    # Re-registering must discard both rising and falling pending flags.
    events[0] = 0
    pin.irq(handler=hard_edge, trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, hard=True)
    state = disable_irq()
    try:
        pin(1)
        pin(0)
        pin.irq(handler=hard_edge, trigger=Pin.IRQ_RISING, hard=True)
    finally:
        enable_irq(state)
    time.sleep_ms(2)
    assert events[0] == 0
    pin(1)
    wait_count(events, 0, 1)
    pin(0)

    events[0] = 0
    install_soft()
    gc.collect()
    pin(1)
    wait_count(events, 0, 1)
    pin(0)
    wait_count(events, 0, 2)
    assert events[1][0] is pin
    pin.irq(handler=None)
    pin(1)
    pin(0)
    time.sleep_ms(2)
    assert events[0] == 2

    def legacy_led(arg):
        assert arg == line
        legacy_events[0] += 1

    def legacy_button(arg):
        assert arg == 13
        legacy_events[1] += 1

    for source, handler in ((pin, legacy_led), (Pin.cpu.C13, legacy_button)):
        extints.append((source, ExtInt(source, ExtInt.IRQ_RISING, Pin.PULL_NONE, handler)))
    try:
        pin.irq(handler=hard_edge, trigger=Pin.IRQ_RISING)
    except OSError:
        pass
    else:
        raise AssertionError("Pin.irq cannot take a line owned by ExtInt")
    state = disable_irq()
    try:
        for source, ext in extints:
            ext.swint()
    finally:
        enable_irq(state)
    wait_count(legacy_events, 0, 1)
    wait_count(legacy_events, 1, 1)

    ext = extints[0][1]
    ext.disable()
    ext.swint()
    time.sleep_ms(2)
    assert legacy_events[0] == 1
    ext.enable()
    time.sleep_ms(2)  # Allow any masked pending event to be delivered.
    before = legacy_events[0]
    ext.swint()
    wait_count(legacy_events, 0, before + 1)
    print("C5 EXTI PASS")
finally:
    for source, ext in extints:
        ext.disable()
        ExtInt(source, ExtInt.IRQ_RISING, Pin.PULL_NONE, None)
    pin.irq(handler=None)
    pin.init(Pin.OUT, value=original)
