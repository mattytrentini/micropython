# Test machine.OctoSPI: construction, basic transfers, and error paths.
#
# This test doesn't require any external wiring -- OctoSPI's write/read just
# need to complete without hanging or crashing; there's no slave device
# attached to validate real octal-line framing against.

try:
    from machine import OctoSPI
except ImportError:
    print("SKIP")
    raise SystemExit

try:
    from target_wiring import octospi_standalone_args_list
except ImportError:
    print("SKIP")
    raise SystemExit


def test_bad_args(id_, kwargs):
    # Missing pins.
    try:
        OctoSPI(id_)
        print("FAIL: expected ValueError for missing pins")
    except ValueError:
        print("OK: missing pins rejected")

    # Invalid id.
    try:
        OctoSPI(id_ + 100, **kwargs)
        print("FAIL: expected ValueError for invalid id")
    except ValueError:
        print("OK: invalid id rejected")


def test_instance(args, kwargs):
    o = OctoSPI(*args, **kwargs)

    o.write(b"12345678")
    buf = o.read(8)
    assert len(buf) == 8
    buf = bytearray(8)
    o.readinto(buf)
    assert len(buf) == 8
    print("OK: write/read/readinto completed")

    # firstbit must be MSB (0); LSB (1) is rejected.
    try:
        o.init(firstbit=1)
        print("FAIL: expected ValueError for firstbit=LSB")
    except ValueError:
        print("OK: firstbit=LSB rejected")

    o.deinit()

    # Using a deinitialized bus should raise.
    try:
        o.write(b"x")
        print("FAIL: expected OSError after deinit")
    except OSError:
        print("OK: write after deinit rejected")

    # Reconstruct to leave things in a clean, working state.
    o2 = OctoSPI(*args, **kwargs)
    o2.write(b"x")
    o2.deinit()
    print("OK: reconstruct after deinit")


for args, kwargs in octospi_standalone_args_list:
    test_bad_args(args[0], kwargs)
    test_instance(args, kwargs)
