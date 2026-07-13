# Target wiring for general esp32 board.
#
# Connect:
# - GPIO4 to GPIO5
# - GPIO12 to GPIO13

import sys

uart_loopback_args = (1,)
uart_loopback_kwargs = {"tx": 4, "rx": 5}

if "ESP32-C" in sys.implementation._machine:
    spi_standalone_args_list = [(1,)]
else:
    spi_standalone_args_list = [(1,), (2,)]

# QuadSPI has no safe default pins (they usually overlap with the module's own
# flash/PSRAM), so pins must always be picked explicitly. Verified free/floating
# on a classic ESP32 devkit; other variants may need different pin choices.
if "ESP32-C" not in sys.implementation._machine:
    quadspi_standalone_args_list = [
        ((1,), {"sck": 18, "io0": 19, "io1": 21, "io2": 22, "io3": 23}),
    ]

pwm_loopback_pins = [(4, 5)]

encoder_loopback_id = 0
encoder_loopback_out_pins = (4, 12)
encoder_loopback_in_pins = (5, 13)
