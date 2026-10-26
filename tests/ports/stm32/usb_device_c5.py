# Exercise runtime USB reconfiguration on STM32C5. Run over the board UART REPL,
# because each active-state change makes the native USB host re-enumerate.
import machine
import sys

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

usb = machine.USBDevice()
usb.active(False)

# Activate the built-in CDC configuration explicitly.
usb.builtin_driver = usb.BUILTIN_CDC
usb.active(True)
assert usb.active()
usb.active(False)
assert not usb.active()

# Configure a real vendor-specific interface descriptor and activate it without
# built-in drivers, then restore CDC so the board remains recoverable over USB.
usb.builtin_driver = usb.BUILTIN_NONE
usb.config(
    desc_dev=bytes((
        18, 1, 0, 2, 0, 0, 0, 64,
        0x09, 0x12, 0xC5, 0xC5, 0, 1, 1, 2, 3, 1,
    )),
    desc_cfg=bytes((
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 0xFF, 0, 0, 0,
        7, 5, 0x81, 2, 64, 0, 0,
        7, 5, 0x01, 2, 64, 0, 0,
    )),
    desc_strs={1: "MicroPython", 2: "C5 runtime", 3: "0001"},
)
usb.active(True)
assert usb.active()
usb.active(False)
assert not usb.active()

usb.builtin_driver = usb.BUILTIN_CDC
usb.config(
    desc_dev=usb.BUILTIN_CDC.desc_dev,
    desc_cfg=usb.BUILTIN_CDC.desc_cfg,
    desc_strs=None,
)
usb.active(True)
assert usb.active()
print("C5 USBDevice configuration and CDC recovery state passed")
