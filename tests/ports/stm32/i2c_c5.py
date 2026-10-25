# C5 controller errors, lifecycle, and transfers without external wiring.
# Leave the Nucleo I2C pins unconnected. The XIAO uses its onboard LSM6DS3.
import sys
import time
from machine import I2C, Pin

board = sys.implementation._machine
if "NUCLEO_C5A3ZG" in board:
    buses = (1, 2)
    imu_bus = None
elif "XIAO STM32C5" in board:
    buses = (1, 2)
    imu_bus = 2
else:
    print("SKIP")
    raise SystemExit


def expect_oserror(errno, operation):
    try:
        operation()
    except OSError as error:
        assert error.args[0] == errno, error
    else:
        raise AssertionError("expected OSError", errno)


for bus in buses:
    i2c = I2C(bus, freq=100_000, timeout=5000)
    buffer = bytearray(b"unchanged")
    expect_oserror(19, lambda: i2c.readfrom_into(0x55, buffer))  # ENODEV
    assert buffer == b"unchanged"
    expect_oserror(19, lambda: i2c.writeto(0x55, b""))
    expect_oserror(19, lambda: i2c.writevto(0x55, (b"", b"data", b"")))

    for kwargs in ({"freq": 0}, {"freq": -1}, {"freq": 99_999},
                   {"timeout": -1}, {"timeout": 65_535_001}):
        expect_oserror(22, lambda: I2C(bus, **kwargs))  # EINVAL, not truncated deadlines.
        expect_oserror(22, lambda: i2c.init(**kwargs))

    for freq in (100_000, 400_000, 1_000_000):
        i2c.init(freq=freq, timeout=5000)
        expect_oserror(19, lambda: i2c.writeto(0x55, b"data"))

    i2c.deinit()
    i2c.deinit()
    expect_oserror(19, lambda: i2c.writeto(0x55, b""))
    i2c.init(freq=100_000, timeout=5000)
    expect_oserror(19, lambda: i2c.writeto(0x55, b""))
    i2c.deinit()
    i2c = I2C(bus, freq=100_000, timeout=5000)
    expect_oserror(19, lambda: i2c.writeto(0x55, b"data"))
    if bus != imu_bus:
        assert i2c.scan() == []

if imu_bus is None:
    # Different deadlines check that initializing one controller does not alter another.
    faults = ((1, Pin.cpu.B6, 4, 5000, 5), (2, Pin.cpu.B3, 9, 30000, 30))
    controllers = [I2C(bus, freq=100_000, timeout=timeout)
                   for bus, scl, af, timeout, ms in faults]
    for (bus, scl, af, timeout, timeout_ms), i2c in zip(faults, controllers):
        try:
            # An internal pull-down holds the actual AF open-drain SCL pad low.
            scl.init(Pin.ALT_OPEN_DRAIN, pull=Pin.PULL_DOWN, af=af)
            assert scl.value() == 0
            started = time.ticks_ms()
            expect_oserror(110, lambda: i2c.writeto(0x55, b""))  # ETIMEDOUT
            elapsed = time.ticks_diff(time.ticks_ms(), started)
            assert timeout_ms - 1 <= elapsed < timeout_ms + 15, elapsed
        finally:
            # Restore the pull-up and peripheral after each failed transfer.
            i2c.init(freq=100_000, timeout=timeout)

        assert i2c.scan() == []
        expect_oserror(19, lambda: i2c.writeto(0x55, b"data"))
else:
    i2c = I2C(imu_bus, freq=100_000, timeout=5000)
    addresses = [addr for addr in i2c.scan() if addr in (0x6A, 0x6B)]
    assert len(addresses) == 1
    address = addresses[0]
    for freq in (100_000, 400_000):
        i2c.init(freq=freq, timeout=5000)
        identity = i2c.readfrom_mem(address, 0x0F, 1)
        assert identity in (b"\x69", b"\x6a")  # LSM6DS3 variants.
        buffer = bytearray(1)
        i2c.readfrom_mem_into(address, 0x0F, buffer)
        assert buffer == identity
        assert i2c.writeto(address, b"") == 0
        assert i2c.writevto(address, (b"", b"\x0f", b""), False) == 1
        i2c.readfrom_into(address, buffer)
        assert buffer == identity
        assert i2c.writevto(address, (b"", b"")) == 0
        assert i2c.readfrom(address, 0) == b""
        expect_oserror(19, lambda: i2c.writeto(0x55, b"data"))
        assert i2c.readfrom_mem(address, 0x0F, 1) == identity
        i2c.deinit()
        i2c.init(freq=freq, timeout=5000)
        assert i2c.readfrom_mem(address, 0x0F, 1) == identity

print("C5 I2C CONTROLLER PASS")
