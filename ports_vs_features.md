# MicroPython Ports vs Features

Port support for `machine` module classes and related features.

| | ✅ Supported | ⚠️ Partial / board-dependent | ❌ Not available |
|---|---|---|---|

> **Note:** This table covers the active, non-legacy ports. CC3200/WiPy and pic16bit are excluded.
> SPI and I2C entries cover hardware peripherals; all ports also provide software fallbacks (`SoftSPI`, `SoftI2C`).

## Feature Matrix

| Feature | ESP32 | ESP8266 | RP2 | STM32 | i.MXRT | nRF | SAMD | Renesas-RA | Alif | Zephyr |
|---------|:-----:|:-------:|:---:|:-----:|:------:|:---:|:----:|:----------:|:----:|:------:|
| `Pin` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `UART` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `SPI` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `I2C` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `PWM` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `ADC` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `DAC` | ⚠️ [1] | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| `Timer` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `RTC` | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ |
| `WDT` | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| `I2S` | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| `I2CTarget` | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ |
| `CAN` | ❌ | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| `USBDevice` | ✅ | ❌ | ✅ | ❌ [2] | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ |
| `SDCard` | ✅ | ❌ | ❌ | ⚠️ [3] | ✅ | ❌ | ❌ | ✅ | ❌ | ❌ |
| `Counter` | ✅ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| `Encoder` | ✅ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| BLE | ✅ | ❌ | ⚠️ [4] | ⚠️ [5] | ❌ | ⚠️ [6] | ❌ | ❌ | ❌ | ✅ |

## Notes

1. **ESP32 DAC**: Only available on chips with DAC hardware — original ESP32 and ESP32-S2. Not available on ESP32-S3, C3, C6, H2.
2. **STM32 USBDevice**: The code path exists but requires `MICROPY_HW_TINYUSB_STACK`, which defaults to 0 and no shipped board enables it.
3. **STM32 SDCard**: Board-dependent; many F4/H7 boards include SD card hardware, but it is not universally available.
4. **RP2 BLE**: Available on Pico W (CYW43439) and other boards with a wireless module. Not available on standard Pico.
5. **STM32 BLE**: Available on STM32WB-series boards only.
6. **nRF BLE**: The nRF port targets nRF51/nRF52 microcontrollers which include a hardware BLE radio, but `ubluetooth` support requires a Nordic SoftDevice and is board-dependent.
