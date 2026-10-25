# STM32C5 uses 32 word-sized TAMP backup registers.
import machine
import sys

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

region = machine.mem_backup()
assert region.itemsize == 4
assert len(region) == 32
assert len(region) * region.itemsize == 128

regions = machine.mem_backup(-1)
assert len(regions) == 1
assert regions[0].itemsize == 4 and len(regions[0]) == 32

original = region[0]
region[0] = 0x13579BDF
assert region[0] == 0x13579BDF
region[0] = original

for index in (-2, 1):
    try:
        machine.mem_backup(index)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid backup-memory region accepted")

print("C5 backup-memory register region")
