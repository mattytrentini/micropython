# Exercise more than one 8-KiB internal-flash cache page through real file I/O.
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

    # The file is larger than the entire flash cache; reopening must read
    # previously evicted data, not only the current RAM cache contents.
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
