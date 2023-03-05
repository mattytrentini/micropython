from micropython import const
from machine import Pin

# M5STACK ATOMS3 Hardware Pin Assignments
"""
        BACK
|3V3|
| G5| IR    G4 |G39|
| G6| BTN  G41 |G38|
| G7|          | 5V|
| G8|          |GND|
    GND 5V G2 G1
     Grove Port
"""

# Button
BUTTON_PIN = const(41)

# IR
IR_PIN = const(4)

# Grove port
GROVE_PORT_PIN = (const(2), const(1))


class ATOMS3:
    def __init__(self, np_n):
        self._btn = Pin(BUTTON_PIN, Pin.IN, Pin.PULL_UP)

    def get_button_status(self):
        return self._btn.value()

    def set_button_callback(self, cb):
        self._btn.irq(trigger=Pin.IRQ_FALLING, handler=cb)
