# The peer stays silent so no other node can ACK the DUT's requests.
# Unlike the babbler test, this does not depend on continuous bus saturation.
from machine import CAN
import time

can = CAN(1, 500_000)
completed = [0] * CAN.TX_QUEUE_LEN
failed = [0] * CAN.TX_QUEUE_LEN


def irq(c):
    while flags := c.irq().flags():
        assert flags & CAN.IRQ_TX
        idx = (flags >> CAN.IRQ_TX_IDX_SHIFT) & CAN.IRQ_TX_IDX_MASK
        assert idx < CAN.TX_QUEUE_LEN
        completed[idx] += 1
        if flags & CAN.IRQ_TX_FAILED:
            failed[idx] += 1


def wait_completed(count):
    deadline = time.ticks_add(time.ticks_ms(), 2000)
    while sum(completed) != count:
        assert time.ticks_diff(deadline, time.ticks_ms()) > 0, (completed, failed)
        time.sleep_ms(1)


def instance0():
    can.set_filters([])
    can.irq(irq, CAN.IRQ_TX, hard=True)
    multitest.next()
    multitest.wait("peer silent")
    slots = [can.send(0x300 + i, b"pending") for i in range(CAN.TX_QUEUE_LEN)]
    assert None not in slots and len(set(slots)) == CAN.TX_QUEUE_LEN
    assert can.send(0x400, b"full") is None
    time.sleep_ms(20)
    assert can.get_counters()[5] == CAN.TX_QUEUE_LEN
    assert completed == [0] * CAN.TX_QUEUE_LEN
    print("no-ACK requests remain pending")

    for i, slot in enumerate(slots):
        assert can.cancel_send(slot)
        wait_completed(i + 1)
        assert failed[slot] == completed[slot] == 1
        assert not can.cancel_send(slot)
        replacement = can.send(0x300 + i, b"replace")
        assert replacement == slot
        assert can.get_counters()[5] == CAN.TX_QUEUE_LEN
    assert completed == failed == [1] * CAN.TX_QUEUE_LEN
    print("indexed cancellations and slot reuse")

    multitest.broadcast("cancellation verified")
    multitest.wait("peer normal")
    wait_completed(2 * CAN.TX_QUEUE_LEN)
    assert completed == [2] * CAN.TX_QUEUE_LEN
    assert failed == [1] * CAN.TX_QUEUE_LEN
    assert can.get_counters()[5] == 0
    print("normal-mode completions")
    time.sleep_ms(20)
    assert completed == [2] * CAN.TX_QUEUE_LEN
    assert all(not can.cancel_send(slot) for slot in slots)
    print("no duplicate completions")
    can.deinit()
    multitest.broadcast("sender done")


def instance1():
    can.init(500_000, mode=CAN.MODE_SILENT)
    can.set_filters([])
    multitest.next()
    multitest.broadcast("peer silent")
    multitest.wait("cancellation verified")
    can.init(500_000, mode=CAN.MODE_NORMAL)
    can.set_filters([])
    multitest.broadcast("peer normal")
    multitest.wait("sender done")
    can.deinit()
    print("peer acknowledged pending queue")
