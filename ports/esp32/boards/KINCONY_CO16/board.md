The KinCony CO16 is a DIN-rail smart controller based on the
ESP32-S3-WROOM-1U-N16R8, with 16 MiB flash and 8 MiB octal PSRAM.

This board definition provides standard MicroPython with WiFi, BLE, native USB,
and the existing ESP32 W5500 Ethernet support. It sets bus defaults and exposes
named pins; it does not initialise the relays, digital inputs, ADCs, RTC, display
or optional modules. Peripheral drivers must be installed separately.

Hardware references:

- [Product page](https://www.kincony.com/esp32-s3-16-channel-analog-input-smart-controller.html)
- [Manufacturer pinout](https://www.kincony.com/forum/showthread.php?tid=9778)

### Building

From the MicroPython repository root, build using the container-based `mpbuild`:

```bash
mpbuild build KINCONY_CO16
```

The combined firmware image is `ports/esp32/build-KINCONY_CO16/firmware.bin`.
Flash it at offset `0`. The standard ESP32 expandable partition layout reserves
2 MiB for system data and firmware, leaving the remaining flash for filesystems.

### USB console

The REPL is on native USB through the USB-C connector. The UART REPL and ESP-IDF
console are disabled because GPIO43 and GPIO44 are used by the W5500.
To enter download mode, hold the ESP32 Download button, press and release ESP32
Reset, then release Download. Do not use the Tuya configuration button.

### Default buses

| MicroPython interface | Connection | GPIOs |
| --- | --- | --- |
| `I2C(0)` | On-board I2C devices | SDA 8, SCL 18 |
| `SPI(1)` | Display, SD card, MAX31865, optional LoRa | SCK 11, MOSI 10, MISO 12 |
| `SPI(2)` | W5500 Ethernet | SCK 1, MOSI 2, MISO 41 |
| `UART(0)` | Optional SIM7600 or KNX module | TX 48, RX 47 |
| `UART(1)` | RS485 | TX 39, RX 38 |
| `UART(2)` | Tuya module | TX 16, RX 17 |

Pin aliases from `pins.csv` can be used with `Pin`, for example `Pin("ETH_CS")`.
Serial baud rates and protocols must be selected for the connected device.

### I2C devices

| Device | Function | 7-bit address |
| --- | --- | --- |
| PCF8575 | Relays 1-16 | `0x22` |
| XL9555 | Digital inputs 1-16 | `0x24` |
| ADS1115 U35 | Analog inputs 1-4 | `0x48` |
| ADS1115 U7 | Analog inputs 5-8 | `0x49` |
| ADS1115 U15 | Analog inputs 9-12 | `0x4b` |
| ADS1115 U19 | Analog inputs 13-16 | `0x4a` |
| 24C02 EEPROM | Non-volatile storage | `0x50` |
| DS3231 | RTC | `0x68` |

Scan the bus without changing relay outputs:

```python
from machine import I2C

i2c = I2C(0, freq=100_000)
print([hex(address) for address in i2c.scan()])
```

The analog inputs use external ADS1115 converters, not `machine.ADC`.
Select 0-10 V or 4-20 mA using each channel's hardware jumper as described by
KinCony; a driver must apply the appropriate scaling.

### Ethernet

Initialise the W5500 explicitly using the built-in ESP32 driver:

```python
import network
from machine import Pin, SPI

lan = network.LAN(
    spi=SPI(2),
    phy_type=network.PHY_W5500,
    phy_addr=0,
    cs=Pin("ETH_CS"),
    int=Pin("ETH_INT"),
    reset=Pin("ETH_RST"),
)
lan.active(True)
```

### Shared SPI peripherals

| Device | Control signals (GPIOs) |
| --- | --- |
| ST7789 display | CS 4, DC 0, reset 5, backlight 40 |
| SD card | CS 9 |
| MAX31865 | CS 14, ready 21 |
| NX3L4051 PT100 multiplexer | S1 7, S2 21 |
| Optional LoRa module | CS 13, reset 5, interrupt 40 |
| 433 MHz receiver | Input 6 (not an SPI device) |

Application code must manage chip selects on the shared bus; deselect unused
SPI devices before transfers. Display reset/backlight share GPIO5/40 with LoRa
reset/interrupt, and the manufacturer lists GPIO21 for both the PT100 multiplexer
S2 and MAX31865 ready signal. These aliases do not imply independent signals.
GPIO0 is both the Download button and the display DC signal.

The SD card defaults are provided for `machine.SDCard(slot=2)`. The ESP32 SDCard
SPI driver owns that SPI host exclusively, so it cannot run concurrently with
`SPI(1)` peripherals. Use a separate SPI SD-card driver if the application needs
to share the bus with the display, MAX31865 or LoRa.
