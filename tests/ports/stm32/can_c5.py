# Internal loopback: no CAN traffic is transmitted onto an external bus.
import sys
import time

if "STM32C5" not in sys.implementation._machine:
    print("SKIP")
    raise SystemExit

try:
    from machine import CAN, disable_irq, enable_irq
except ImportError:
    print("SKIP")
    raise SystemExit


def wait_for(predicate):
    deadline = time.ticks_add(time.ticks_ms(), 1000)
    while not predicate():
        assert time.ticks_diff(deadline, time.ticks_ms()) > 0, "CAN event timeout"
        time.sleep_ms(1)


def receive(can, ident, payload, flags=0):
    deadline = time.ticks_add(time.ticks_ms(), 500)
    while True:
        message = can.recv()
        if message is not None:
            assert message[0] == ident, message
            assert bytes(message[1]) == payload, message
            assert message[2] == flags, message
            assert not message[3] & CAN.RECV_ERR_OVERRUN, message
            return
        assert time.ticks_diff(deadline, time.ticks_ms()) > 0, "CAN receive timeout"
        time.sleep_ms(1)


can = CAN(1, 500_000, mode=CAN.MODE_SILENT_LOOPBACK)
try:
    can.set_filters(None)
    for ident, flags in ((0, 0), (0x7FF, 0), (0x1FFFFFFF, CAN.FLAG_EXT_ID)):
        for length in range(9):
            payload = bytes((length + i * 37) & 255 for i in range(length))
            assert can.send(ident, payload, flags) is not None
            receive(can, ident, payload, flags)
    assert can.send(0x123, b"12345678", CAN.FLAG_RTR) is not None
    receive(can, 0x123, bytes(8), CAN.FLAG_RTR)
    print("C5 CAN classic identifiers, payloads and RTR")

    can.set_filters(((0x300, 0x700, 0), (0x1234500, 0x1FFFFF00, CAN.FLAG_EXT_ID)))
    assert can.send(0x325, b"std") is not None
    receive(can, 0x325, b"std")
    assert can.send(0x12345AB, b"ext", CAN.FLAG_EXT_ID) is not None
    receive(can, 0x12345AB, b"ext", CAN.FLAG_EXT_ID)
    assert can.send(0x400, b"reject") is not None
    time.sleep_ms(5)
    assert can.recv() is None
    print("C5 CAN standard and extended filter masks")

    can.init(10_000, mode=CAN.MODE_SILENT_LOOPBACK)
    can.set_filters(None)
    events = []

    def callback(controller):
        events.append(controller.irq().flags())

    can.irq(callback, CAN.IRQ_TX)
    irq_state = disable_irq()
    try:
        slots = [can.send(0x300 + i, b"12345678") for i in range(CAN.TX_QUEUE_LEN)]
        assert None not in slots and len(set(slots)) == CAN.TX_QUEUE_LEN
        assert can.get_counters()[5] == CAN.TX_QUEUE_LEN
        # Coalesce all hardware completions into one pending interrupt.
        time.sleep_us(60_000)
        assert can.get_counters()[5] == 0
    finally:
        enable_irq(irq_state)
    wait_for(lambda: len(events) == CAN.TX_QUEUE_LEN)
    assert all(flags & CAN.IRQ_TX and not flags & CAN.IRQ_TX_FAILED for flags in events)
    indices = [(flags >> CAN.IRQ_TX_IDX_SHIFT) & CAN.IRQ_TX_IDX_MASK for flags in events]
    assert sorted(indices) == sorted(slots)
    for i in range(CAN.TX_QUEUE_LEN):
        receive(can, 0x300 + i, b"12345678")
    print("C5 CAN coalesced indexed completions")

    events.clear()
    irq_state = disable_irq()
    try:
        slots = [can.send(0x300 + i, b"12345678") for i in range(CAN.TX_QUEUE_LEN)]
        assert None not in slots
        assert can.cancel_send(slots[-1])
    finally:
        enable_irq(irq_state)
    wait_for(lambda: len(events) == CAN.TX_QUEUE_LEN)
    failed = [flags for flags in events if flags & CAN.IRQ_TX_FAILED]
    assert len(failed) == 1
    assert (failed[0] >> CAN.IRQ_TX_IDX_SHIFT) & CAN.IRQ_TX_IDX_MASK == slots[-1]
    for i in range(CAN.TX_QUEUE_LEN - 1):
        receive(can, 0x300 + i, b"12345678")
    assert can.recv() is None
    can.irq(None, 0)
    assert can.send(0x456, b"again") is not None
    receive(can, 0x456, b"again")
    wait_for(lambda: can.get_counters()[5] == 0)
    time.sleep_ms(1)
    # An unconsumed completion is not an outstanding cancellable request.
    can.irq(None, CAN.IRQ_TX)
    completed = can.send(0x457, b"done")
    assert completed is not None
    wait_for(lambda: can.get_counters()[5] == 0)
    assert not can.cancel_send(completed)
    can.irq(None, 0)
    receive(can, 0x457, b"done")
    print("C5 CAN cancellation and IRQ-disable recovery")

    can.init(500_000, mode=CAN.MODE_SILENT_LOOPBACK)
    can.set_filters(None)
    for hard in (False, True):
        irq_flags = [0]

        def irq_callback(controller):
            irq_flags[0] |= controller.irq().flags()

        can.irq(irq_callback, CAN.IRQ_RX | CAN.IRQ_TX, hard=hard)
        assert can.send(0x123, b"irq") is not None
        wait_for(lambda: irq_flags[0] & (CAN.IRQ_RX | CAN.IRQ_TX) == CAN.IRQ_RX | CAN.IRQ_TX)
        can.irq(None, 0)
        receive(can, 0x123, b"irq")
    print("C5 CAN soft and hard RX/TX callbacks")
finally:
    can.deinit()
assert can.state() == CAN.STATE_STOPPED
