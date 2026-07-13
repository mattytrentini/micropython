# Test machine.QuadSPI: construction, basic transfers, and error paths.
#
# This test doesn't require any external wiring -- QuadSPI's write/read just
# need to complete without hanging or crashing; there's no slave device
# attached to validate real quad-line framing against.

try:
    from machine import QuadSPI
except ImportError:
    print("SKIP")
    raise SystemExit

try:
    from target_wiring import quadspi_standalone_args_list
except ImportError:
    print("SKIP")
    raise SystemExit


def test_bad_args(id_, kwargs):
    # Missing pins.
    try:
        QuadSPI(id_)
        print("FAIL: expected ValueError for missing pins")
    except ValueError:
        print("OK: missing pins rejected")

    # Invalid id.
    try:
        QuadSPI(id_ + 100, **kwargs)
        print("FAIL: expected ValueError for invalid id")
    except ValueError:
        print("OK: invalid id rejected")


def test_instance(args, kwargs):
    q = QuadSPI(*args, **kwargs)

    q.write(b"12345678")
    buf = q.read(8)
    assert len(buf) == 8
    buf = bytearray(8)
    q.readinto(buf)
    assert len(buf) == 8
    print("OK: write/read/readinto completed")

    # firstbit must be MSB (0); LSB (1) is rejected.
    try:
        q.init(firstbit=1)
        print("FAIL: expected ValueError for firstbit=LSB")
    except ValueError:
        print("OK: firstbit=LSB rejected")

    q.deinit()

    # Using a deinitialized bus should raise.
    try:
        q.write(b"x")
        print("FAIL: expected OSError after deinit")
    except OSError:
        print("OK: write after deinit rejected")

    # Reconstruct to leave things in a clean, working state.
    q2 = QuadSPI(*args, **kwargs)
    q2.write(b"x")
    q2.deinit()
    print("OK: reconstruct after deinit")


for args, kwargs in quadspi_standalone_args_list:
    test_bad_args(args[0], kwargs)
    test_instance(args, kwargs)
