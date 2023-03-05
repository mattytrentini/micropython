from micropython import const
from machine import Pin, I2C
import neopixel
from ssd1306 import SSD1306_I2C

"""
          TOP                     BOTTOM
        +-----+                   +-----+
+-------|     |-------+   +-------!-----!-------+
||RGB39||     |   |BAT|   |{BUT0}       {RESET} |
|       +-----+   | 5V|   |                     |
|G21|             |GND|   |                     |
|G25| +---------+ |3V3|   |                     |
| 5V| | OLED    | |G35|   |                     |
|GNG| |  SCL40  | |G36|   |                     |
|GNG| |  SDA41  | |G36|   |          +--------+ |
|GNG| +---------+ |G36|   |          |G|5V|2|1| |
+---------------------+   +----------+--------+-+

I2C0 is allocated to the STEMMA QT (expansion) port
I2C1 is allocated to the OLED Display
"""

# WS2812
WS2812_PIN = const(39)

# Button
BUTTON_PIN = const(0)

Button = Pin(BUTTON_PIN, Pin.IN, Pin.PULL_UP)
RGBLED = neopixel.NeoPixel(Pin(WS2812_PIN), 1)
Display = SSD1306_I2C(72, 40, I2C(1))