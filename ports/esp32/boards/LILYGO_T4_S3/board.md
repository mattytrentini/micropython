The following files are firmware for the LILYGO T4-S3
(ESP32-S3R8 with a 2.41" 600x450 RM690B0 AMOLED display driven over QuadSPI,
CS226SE capacitive touch, SY6970 PMU, and a microSD slot).

The display's QuadSPI bus is available as `machine.QuadSPI(1)` using this
board's default pins; the display's CS/RESET pins and the touch/PMU I2C bus
are plain GPIO/`machine.I2C` and are driven directly by a display driver, not
part of the bus itself (this port's QuadSPI has no hardware CS -- see
ports/esp32/machine_hw_quadspi.c).

This board uses native USB for the serial REPL.
