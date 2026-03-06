"""
Waveshare ESP32-S3-POE-ETH-8DI-8DO board helper module.

Provides pin constants, ethernet initialisation, and classes for
the 8 optocoupler-isolated digital inputs and 8 digital outputs
(via TCA9554PWR I2C IO expander).
"""

from machine import Pin, SPI, SoftI2C

# Digital inputs (optocoupler-isolated, accent-low)
DI1 = 4
DI2 = 5
DI3 = 6
DI4 = 7
DI5 = 8
DI6 = 9
DI7 = 10
DI8 = 11

# W5500 Ethernet
ETH_MOSI = 13
ETH_MISO = 14
ETH_SCK = 15
ETH_CS = 16
ETH_INT = 12
ETH_RST = 39

# RS485
RS485_TX = 17
RS485_RX = 18
RS485_RTS = 21

# CAN bus
CAN_TX = 2
CAN_RX = 3

# SD card
SD_D0 = 45
SD_CMD = 47
SD_SCK = 48

# RTC (PCF85063) and IO expander (TCA9554PWR) share I2C bus
RTC_INT = 40
RTC_SCL = 41
RTC_SDA = 42

# RGB LED and buzzer
RGB_PIN = 38
BUZZER_PIN = 46

# TCA9554PWR I2C IO expander for digital outputs
TCA9554_ADDR = 0x20

# TCA9554 registers
_REG_INPUT = 0x00
_REG_OUTPUT = 0x01
_REG_POLARITY = 0x02
_REG_CONFIG = 0x03


def init_eth():
    """Initialise W5500 SPI ethernet and return the LAN interface."""
    import network

    Pin(ETH_RST, Pin.OUT).value(1)  # ensure not in reset
    spi = SPI(2, baudrate=25_000_000)
    lan = network.LAN(
        phy_type=network.PHY_W5500,
        phy_addr=1,
        spi=spi,
        cs=Pin(ETH_CS),
        int=Pin(ETH_INT),
    )
    lan.active(True)
    return lan


class DigitalInputs:
    """Read the 8 optocoupler-isolated digital inputs (GPIO 4-11)."""

    _PINS = (DI1, DI2, DI3, DI4, DI5, DI6, DI7, DI8)

    def __init__(self):
        self._pins = [Pin(p, Pin.IN) for p in self._PINS]

    def read(self, channel=None):
        """Read one channel (0-7) or all channels as a tuple of 8 values."""
        if channel is not None:
            return self._pins[channel].value()
        return tuple(p.value() for p in self._pins)

    def __getitem__(self, idx):
        return self._pins[idx].value()

    def __len__(self):
        return 8


class DigitalOutputs:
    """Control 8 digital outputs via TCA9554PWR I2C IO expander."""

    def __init__(self, i2c=None):
        if i2c is None:
            i2c = SoftI2C(scl=Pin(RTC_SCL), sda=Pin(RTC_SDA))
        self._i2c = i2c
        self._addr = TCA9554_ADDR
        self._state = 0x00
        # Configure all 8 pins as outputs (0 = output)
        self._i2c.writeto_mem(self._addr, _REG_CONFIG, bytes([0x00]))
        # Set all outputs low initially
        self._i2c.writeto_mem(self._addr, _REG_OUTPUT, bytes([self._state]))

    def write(self, channel, value):
        """Set a single output channel (0-7) high (1) or low (0)."""
        if value:
            self._state |= 1 << channel
        else:
            self._state &= ~(1 << channel)
        self._i2c.writeto_mem(self._addr, _REG_OUTPUT, bytes([self._state]))

    def write_all(self, value):
        """Set all 8 outputs at once from an 8-bit value."""
        self._state = value & 0xFF
        self._i2c.writeto_mem(self._addr, _REG_OUTPUT, bytes([self._state]))

    def read(self):
        """Read the current output state as an 8-bit value."""
        return self._state

    def __setitem__(self, idx, val):
        self.write(idx, val)

    def __getitem__(self, idx):
        return (self._state >> idx) & 1

    def __len__(self):
        return 8
