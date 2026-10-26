# A bootloader/debugger can leave CYCCNTENA set while DEMCR gates its clock.
import sys
import time
from machine import disable_irq, enable_irq, freq, mem32

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

DWT_CTRL = 0xE0001000
DEMCR = 0xE000EDFC
TRCENA = 0x01000000
original_ctrl, original_demcr = mem32[DWT_CTRL], mem32[DEMCR]
try:
    mem32[DWT_CTRL] |= 1
    mem32[DEMCR] &= ~TRCENA
    before = time.ticks_cpu()
    time.sleep_us(200)
    elapsed = time.ticks_diff(time.ticks_cpu(), before)
    cycles_per_us = freq()[0] // 1000000
    assert 100 * cycles_per_us <= elapsed <= 1000 * cycles_per_us

    before = time.ticks_cpu()
    irq_state = disable_irq()
    try:
        time.sleep_us(200)
    finally:
        enable_irq(irq_state)
    elapsed = time.ticks_diff(time.ticks_cpu(), before)
    assert 200 * cycles_per_us <= elapsed <= 2000 * cycles_per_us
finally:
    mem32[DWT_CTRL] = original_ctrl
    mem32[DEMCR] = original_demcr
print("C5 gated cycle counter recovery OK")
