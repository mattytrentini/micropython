# M5Stack Dial

[M5Stack Dial Docs](https://docs.m5stack.com/en/core/M5Dial)

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
r = RotaryIRQ(pin_num_clk=40, pin_num_dt=41, min_val=0, max_val=30, reverse=False, range_mode=RotaryIRQ.RANGE_WRAP)
val_old = r.value()
while True:
    val_new = r.value()
    if val_old != val_new:
        val_old = val_new
        print('result =', val_new)
    time.sleep_ms(50)
```

## RFID (WS1850S)

I2C address is `0x28`.

Appears to be used in the [RFID 2
Unit](https://shop.m5stack.com/products/rfid-unit-2-ws1850s).

## RTC (RTC8563)

I2C address is `0x51`.

Appears to be used in the [RTC
Unit](https://shop.m5stack.com/products/real-time-clock-rtc-unit-hym8563).

Appears to be working!

- rtc8563.py: RTC driver
- dial.py: Provides more board-specific RTC usage

Possible improvements:

- [ ] Improve interface - namedtuples are ugly and not used elsewhere
 - Should at least use same interface as machine.RTC?
- [ ] Support IRQs
- [ ] Hook the driver into the MicroPython `machine.RTC` system
- [ ] Change `dial.py` interface to be a class
- Improve encapsulation

## Touch Sensor (FT3267)

I2C address is `0x38`.

`LCD_RESET` needs to be pulled high for the touch sensor to become active._It
appears to start in 'hibernation' mode:

```py
Pin.board.LCD_RESET.init(Pin.OUT)
Pin.board.LCD_RESET.on()
[hex(x) for x in i2c.scan()] `'0x38'` will now appear.
# ['0x28', '0x38', '0x51']
```

Simple [Arduino example
driver](https://github.com/mmMicky/TouchLib/blob/main/src/ModulesFT3267.tpp).

Touch works!

```py
def point():
     raw = i2c.readfrom_mem(0x38, 0x03, 4)
     x = ((raw[0] & 0x0f) << 8) + raw[1]
     y = ((raw[2] & 0x0f) << 8) + raw[3]
     return x, y
```

Call `point()` after you touch the screen. Or wire it in to be printed when the
touch sensor triggers an IRQ:

```py
Pin.board.TP_INT.init(Pin.IN)
Pin.board.TP_INT.irq(lambda p: print(point()))
```

## Display (GC9A01)

Evaluating Russ Hughes' [gc9a01py](https://github.com/russhughes/gc9a01py). It's
written in Python (good!) but doesn't use framebuffer so is likely to be slow.

Russ has a C-based version too but it requires firmware to be built. See gc9a01_mpy.

Install (there's no `package.json` but the basic features just require the one
file):

```bash
> mpremote mip install github:russhughes/gc9a01py/lib/gc9a01py.py
```

```py
import gc9a01py as gc9a01
from machine import SPI, Pin
# Initialise as OUT pins
for p in [Pin.board.LCD_BL, Pin.board.LCD_RESET, Pin.board.LCD_RS, Pin.board.LCD_CS]:
    p.init(Pin.OUT)
spi = SPI(1, baudrate=60_000_000)
tft = gc9a01.GC9A01(spi, dc=Pin.board.LCD_RS, cs=Pin.board.LCD_CS, reset=Pin.board.LCD_RESET, backlight=Pin.board.LCD_BL, rotation=0)
tft.fill(gc9a01.BLACK)
```

Unsurprisingly, it turns out to be *much* faster with hardware accelerated SPI!

## Examples

### Touch drawing

Putting the snippets from above...

```py
import gc9a01py as gc9a01
from machine import SPI, Pin, I2C

i2c = I2C(1)

# Toggle LCD RESET to activate touch driver
Pin.board.LCD_RESET.init(Pin.OUT, value=0)
Pin.board.LCD_RESET.on()

# Initialise display
spi = SPI(1, baudrate=60_000_000)
for p in [Pin.board.LCD_BL, Pin.board.LCD_RESET, Pin.board.LCD_RS, Pin.board.LCD_CS]:
    p.init(Pin.OUT)
tft = gc9a01.GC9A01(spi, dc=Pin.board.LCD_RS, cs=Pin.board.LCD_CS, reset=Pin.board.LCD_RESET, backlight=Pin.board.LCD_BL, rotation=0)

# Clear display
tft.fill(gc9a01.BLACK)

# Read the current touch point
def point():
    raw = i2c.readfrom_mem(0x38, 0x03, 4)
    x = ((raw[0] & 0x0f) << 8) + raw[1]
    y = ((raw[2] & 0x0f) << 8) + raw[3]
    return x, y

# Draw a pixel whenever the touch interrupt is raised - at the point where it's touched
Pin.board.TP_INT.init(Pin.IN)
Pin.board.TP_INT.irq(lambda p: tft.pixel(*point(), gc9a01.WHITE))

# Clear the display when the ring button is pressed
Pin.board.WAKE.init(Pin.IN)
Pin.board.WAKE.irq(handler = lambda x: tft.fill(gc9a01.BLACK), trigger=Pin.IRQ_FALLING)
```


## Todo

- [x] On-board definitions
  - [x] Port A - I2Cy -> SCL: G15, SDA: G13
  - [x] Port B - G1/G2 (IN/OUT)
  - [x] I2Cx - SCL: G12, SDA: G11
  - [x] Buzzer: G3
- [x] Display driver
  - [x] GC9A01 SPI
- [x] Touch driver
- [x] RTC driver
  - [x] RTC8563
  - [x] I2C1
- [ ] RFID
  - [ ] WS1850S
  - [ ] I2C1
- [x] Encoder
  - [x] Use Mike's encoder software
  - [x] A:G41, B:G40
  - [x] Button? Maybe WAKE (G40)? What does HOLD do?
- [ ] Update RTC and Dial modules
  - [ ] Add add them to the firmware
