# Exercise multiple 8-KiB internal-flash erase pages through real file I/O.
# Keep ICACHE enabled to catch programming write intrusion and stale flash reads.
import os
import sys
import machine

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

import pyb

if not hasattr(pyb, "Flash"):
    print("SKIP")
    raise SystemExit

# Native mode must use the advertised erase-block size for standard reads too.
# Compare against physical main flash, not another block-protocol method.
if any(type(fs).__name__ == "VfsLfs2" and path == "/flash" for fs, path in os.mount()):
    import uctypes

    board = sys.implementation._machine
    storage_start = 0x08060000 if "WeAct" in board else 0x080C0000
    native = pyb.Flash(start=0)
    assert native.ioctl(1, 1) == 0
    block_size = native.ioctl(5, 0)
    page = bytearray(block_size)
    assert native.readblocks(1, page) == 0
    assert page == uctypes.bytearray_at(storage_start + block_size, block_size)
    subsection = pyb.Flash(start=block_size, len=block_size)
    assert subsection.ioctl(1, 1) == 0
    assert subsection.readblocks(0, page) == 0
    assert page == uctypes.bytearray_at(storage_start + block_size, block_size)
    assert subsection.readblocks(1, page) < 0
    assert subsection.readblocks(0, bytearray(block_size + 1)) < 0
    del page, native, subsection

ICACHE_CR = 0x40030400
ICACHE_SR = ICACHE_CR + 4
ICACHE_FCR = ICACHE_CR + 12
original_cache = machine.mem32[ICACHE_CR]
# Start with a clean error flag; every following flash operation must leave it clear.
machine.mem32[ICACHE_FCR] = 4

filename = "/flash/_c5_flash_roundtrip.bin"
chunk = bytes((i * 37 + 11) & 255 for i in range(4096))
created = False
try:
    machine.mem32[ICACHE_CR] = original_cache | 1
    # Do not replace an existing file if this name is already in use.
    with open(filename, "xb") as f:
        created = True
        for _ in range(8):
            assert f.write(chunk) == len(chunk)
        assert f.write(chunk[:37]) == 37
    os.sync()

    # Reopening must read committed data across several physical erase pages.
    with open(filename, "rb") as f:
        for _ in range(8):
            assert f.read(len(chunk)) == chunk
        assert f.read() == chunk[:37]
    assert os.stat(filename)[6] == 32768 + 37
finally:
    try:
        if created:
            os.remove(filename)
            os.sync()
        assert machine.mem32[ICACHE_CR] & 1
        assert not machine.mem32[ICACHE_SR] & 4
    finally:
        machine.mem32[ICACHE_CR] = original_cache
print("C5 FLASH CACHE BOUNDARIES PASS")
