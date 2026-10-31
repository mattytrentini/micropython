# WeAct Studio STM32C5 100-pin CoreBoard

This definition targets the V1.0 board populated with an STM32C552VET6:
512 KiB program flash, 128 KiB SRAM, an 8 MHz HSE crystal and a 32.768 kHz
LSE crystal. MicroPython runs at 144 MHz. The application starts at
`0x08000000`; the final 128 KiB of program flash is reserved for `/flash`.
There is no resident application bootloader, UF2 volume or MBOOT requirement.

## Initial programming

The USB-C connector is connected directly to the MCU, not a USB/UART bridge.
An application without USB support need not appear as a host device.

The STM32C552 includes a factory system-memory USB DFU bootloader. To request
it, hold K2 (BOOT0), press and release K1 (NRST), then release BOOT0. Boot entry
also depends on the MCU's boot option bytes; do not change them blindly if the
board does not enumerate. Use a data-capable USB cable.

Identify the resulting DFU device and serial number before programming,
especially when other STM32 boards are connected. Use a recent STM32CubeProgrammer
with C5 support to program `firmware.hex`, or, for a device supported by dfu-util:

```sh
dfu-util --list
dfu-util --serial <DFU_SERIAL> --alt 0 --dfuse-address 0x08000000:leave --download firmware.bin
```

Replace `<DFU_SERIAL>` with the identified board's serial number. Do not use a
mass erase when preserving an existing filesystem. Reset with BOOT0 released
after programming. SWD is also available on P5 (SWDIO, SWCLK, NRST, GND and
3.3 V reference) if USB boot entry is unavailable.

## REPL and board pins

The running firmware provides a native USB CDC REPL. A second REPL is available
on P5 TX/RX: USART1 PA9/PA10 at 115200 baud, using 3.3 V UART levels.

GPIO aliases follow the silk-screen names, for example `Pin("A0")`. `LED_BLUE`
is PH15, active high; `SW` is the active-high K3 button on PC13. PH2 is shared
with BOOT0. SWD, USB and oscillator pads are reserved rather than given generic
board aliases. These pads remain addressable through `Pin.cpu` when appropriate.

Default bus assignments (external devices are not fitted):

| Bus | Pins |
| --- | --- |
| UART1 / UART2 / UART3 | TX/RX: PA9/PA10, PA2/PA3, PD8/PD9 |
| UART4 / UART5 | TX/RX: PA0/PA1, PC12/PD2 |
| SPI1 | SCK/MISO/MOSI/NSS: PA5/PA6/PA7/PA4 |
| SPI2 | SCK/MISO/MOSI/NSS: PB13/PB14/PB15/PB12 |
| SPI3 | SCK/MISO/MOSI/NSS: PB3/PB4/PB5/PA15 |
| I2C1 / I2C2 | SCL/SDA: PB6/PB7, PB10/PB12; external pullups required |
| CAN1 | TX/RX: PD1/PD0; external transceiver and bus termination required |
| I2S1 | Supply clock, word-select and data pins explicitly; PA5/PA4/PA7 is a TX option |

I2C2 SDA and SPI2 NSS share PB12. SPI1 and the listed I2S1 option share pins.
USART1's diagnostic REPL must be deinitialized before using PA9/PA10 for other
purposes. This USB-enabled build supports 144 MHz PSIS SYSCLK with 1:1 bus
dividers. Switching to the 8 MHz HSE is rejected: TinyUSB's FSDEV driver requires
an APB clock of at least 10 MHz. ADC and DAC remain disabled, as on the other
C5 boards.

Pin alternate functions are from ST DS14928 Rev 1, Table 13; ADC channel
metadata is from Table 12. Board wiring is documented in the
[vendor schematic](https://github.com/WeActStudio/WeActStudio.STM32C5_100Pin_CoreBoard/blob/master/Hardware/WeAct-STM32C5xxVxTxCoreBoard_V10%20SchDoc.pdf).
