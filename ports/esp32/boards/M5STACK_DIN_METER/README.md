# M5Stack DIN Meter

[M5Stack DIN Meter Docs](https://docs.m5stack.com/en/core/M5DinMeter)

## Rotary Encoder

Mike Teachman's
[micropython-rotary](https://github.com/miketeachman/micropython-rotary) works
great for this encoder.

Install with `mpremote`:

```bash
> mpremote mip install github:miketeachman/micropython-rotary
```

Example:

```py
import time
from rotary_irq_esp import RotaryIRQ
r = RotaryIRQ(pin_num_clk=40, pin_num_dt=41, min_val=0, max_val=30, reverse=True, range_mode=RotaryIRQ.RANGE_WRAP, half_step=True)
val_old = r.value()
while True:
    val_new = r.value()
    if val_old != val_new:
        val_old = val_new
        print('result =', val_new)
    time.sleep_ms(50)
```

## RTC (RTC8563)

I2C address is `0x51`.

Appears to be used in the [RTC
Unit](https://shop.m5stack.com/products/real-time-clock-rtc-unit-hym8563).

Appears to be working!

- rtc8563.py: RTC driver

Possible improvements:

- [ ] Improve interface - namedtuples are ugly and not used elsewhere
 - Should at least use same interface as machine.RTC?
- [ ] Support IRQs
- [ ] Hook the driver into the MicroPython `machine.RTC` system
- Improve encapsulation

## Display (ST7789V2)

```bash
> mpremote mip install github:russhughes/st7789py_mpy/lib/st7789py.py
```

```py
import st7789py as st7789
from machine import SPI, Pin
spi = SPI(1, baudrate=40_000_000)
for p in [Pin.board.LCD_BL, Pin.board.LCD_RESET, Pin.board.LCD_RS, Pin.board.LCD_CS]:
    p.init(Pin.OUT)
tft = st7789.ST7789(spi, 135, 240, reset=Pin.board.LCD_RESET, dc=Pin.board.LCD_RS, cs=Pin.board.LCD_CS, backlight=Pin.board.LCD_BL, rotation=3)
tft.rect(0, 0, 40, 40, st7789.CYAN)
```
