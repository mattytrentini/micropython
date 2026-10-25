# NUCLEO-C5A3ZG only: SPI3 PB3/PB4/PB5 must be unconnected.
# Receive data comes from physical MISO pull levels, not an internal loopback.
import gc
import sys
from machine import SPI, Pin, disable_irq, enable_irq

if "NUCLEO_C5A3ZG" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

miso = Pin.cpu.B4
sck = Pin.cpu.B3
mosi = Pin.cpu.B5
spi = SPI(3, baudrate=600000)
try:
    # Include FIFO-length boundaries and both supported frame widths.
    for bits, lengths in ((8, (1, 15, 16, 257)), (16, (2, 14, 16, 258))):
        for polarity in (0, 1):
            for phase in (0, 1):
                spi.init(bits=bits, polarity=polarity, phase=phase)
                for pull, expected in ((Pin.PULL_UP, 255), (Pin.PULL_DOWN, 0)):
                    miso.init(Pin.ALT, pull=pull, alt=6)
                    for count in lengths:
                        source = bytes((n * 71 + 9) & 255 for n in range(count))
                        dest = bytearray(count)
                        spi.write_readinto(source, dest)
                        assert dest == bytes((expected,)) * count
                        assert sck.value() == polarity
    print("C5 SPI physical receive and modes")

    # Odd-address word buffers preserve word framing and guards.
    spi.init(bits=16)
    miso.init(Pin.ALT, pull=Pin.PULL_UP, alt=6)
    raw = bytearray(37)
    view = memoryview(raw)[1:35]
    spi.readinto(view)
    assert view == b"\xff" * 34
    assert raw[0] == 0 and raw[35:] == b"\x00\x00"
    try:
        spi.write(b"123")
    except ValueError:
        pass
    else:
        raise AssertionError("odd word transfer accepted")
    assert spi.read(2) == b"\xff\xff"
    assert spi.read(0) == b""
    print("C5 SPI word alignment and rejection recovery")

    # Cross the hardware transfer-count limit in each framing mode.
    del source, dest, view, raw
    gc.collect()
    data = bytearray(65536)
    miso.init(Pin.ALT, pull=Pin.PULL_DOWN, alt=6)
    for bits in (8, 16):
        spi.init(bits=bits)
        miso.init(Pin.ALT, pull=Pin.PULL_DOWN, alt=6)
        spi.readinto(data, 0xA5)
        assert not any(data)
    irq = disable_irq()
    try:
        spi.readinto(data, 0xA5)
    finally:
        enable_irq(irq)
    assert not any(data)
    del data
    print("C5 SPI chunking and masked IRQ progress")
    for config in (
        {"baudrate": 0}, {"polarity": 2}, {"phase": 2},
        {"bits": 9}, {"firstbit": 2},
    ):
        try:
            spi.init(**config)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid configuration accepted")
        assert spi.read(2) == b"\x00\x00"
    spi.deinit()
    try:
        spi.read(2)
    except OSError:
        pass
    else:
        raise AssertionError("deinitialized transfer accepted")
    spi.init(bits=8, firstbit=SPI.LSB)
    miso.init(Pin.ALT, pull=Pin.PULL_UP, alt=6)
    assert spi.read(17) == b"\xff" * 17
    print("C5 SPI validation and deinit recovery")
finally:
    spi.deinit()
    miso.init(Pin.ANALOG)
    mosi.init(Pin.ANALOG)
    sck.init(Pin.ANALOG)
    gc.collect()
