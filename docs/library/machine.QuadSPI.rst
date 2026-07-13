.. currentmodule:: machine
.. _machine.QuadSPI:

class QuadSPI -- a Quad SPI bus protocol (controller side)
============================================================

QuadSPI is a variant of `SPI` that uses 4 data lines (``io0``-``io3``) instead
of separate MOSI/MISO lines, giving up to 4x the throughput of a regular SPI
bus of the same clock rate. It's commonly used for external flash, PSRAM,
and other high-bandwidth peripherals that support a quad-data-line mode.

Unlike `SPI`, the 4 data lines are shared/bidirectional (half-duplex): only
one direction can be active on the bus at a time. Because of this, `QuadSPI`
has no ``write_readinto`` method, and `QuadSPI.read`/`QuadSPI.readinto` have
no ``write`` fill-byte parameter -- reading and writing are always separate
transactions. As with `SPI`, management of a CS signal (if used) should
happen in user code via the `machine.Pin` class, which makes it possible to
sequence a `QuadSPI.write` (eg a command) and a `QuadSPI.read` (eg the
response) under a single CS assertion.

Most quad-mode devices are addressed over regular single-line SPI first, to
issue the command that switches the device itself into quad mode. `QuadSPI`
only handles the data phase once both sides already agree they're in quad
mode; a typical sequence is `SPI` for the mode-entry command, then `QuadSPI`
for all subsequent transfers on the same pins.

Only hardware `QuadSPI` is currently supported -- there's no bit-banged
"SoftQuadSPI" equivalent of `SoftSPI`, since bit-banging 4 lines fast enough
to be useful for a real quad peripheral is generally impractical.

Example usage::

    from machine import QuadSPI, SPI, Pin

    cs = Pin(5, mode=Pin.OUT, value=1)

    # Send the mode-entry command over regular 1-line SPI first.
    spi = SPI(1, baudrate=1000000, sck=Pin(18), mosi=Pin(19), miso=Pin(21))
    cs(0)
    spi.write(b"\x35")  # eg enter quad mode on a quad SPI PSRAM chip
    cs(1)
    spi.deinit()

    # Then switch to QuadSPI on the same sck/io0/io1 pins (plus io2/io3).
    qspi = QuadSPI(1, baudrate=20000000, sck=Pin(18), io0=Pin(19), io1=Pin(21),
                   io2=Pin(22), io3=Pin(23))
    cs(0)
    qspi.write(b"\x02\x00\x00\x00" + b"hello")  # eg a quad write command + address + data
    cs(1)

Constructors
------------

.. class:: QuadSPI(id, ...)

   Construct a QuadSPI object on the given bus, *id*. Values of *id* depend
   on a particular port and its hardware.

   With no additional parameters, the QuadSPI object is created but not
   initialised (it has the settings from the last initialisation of
   the bus, if any).  If extra arguments are given, the bus is initialised.
   See ``init`` for parameters of initialisation.

Methods
-------

.. method:: QuadSPI.init(baudrate=1000000, *, polarity=0, phase=0, bits=8, firstbit=QuadSPI.MSB, sck=None, io0=None, io1=None, io2=None, io3=None)

   Initialise the QuadSPI bus with the given parameters:

     - ``baudrate`` is the SCK clock rate.
     - ``polarity`` can be 0 or 1, and is the level the idle clock line sits at.
     - ``phase`` can be 0 or 1 to sample data on the first or second clock edge
       respectively.
     - ``bits`` is the width in bits of each transfer. Only 8 is guaranteed to be supported by all hardware.
     - ``firstbit`` must be ``QuadSPI.MSB`` -- quad-mode hardware is generally
       MSB-first only.
     - ``sck``, ``io0``, ``io1``, ``io2``, ``io3`` are `machine.Pin` objects
       to use for the bus signals. Unlike regular `SPI`, most hardware has no
       safe default quad pins (they frequently overlap with the pins used by
       the device's own flash/PSRAM), so all 5 pins are usually required to
       be given explicitly, either here or as constructor arguments.

   In the case of hardware QuadSPI the actual clock frequency may be lower
   than the requested baudrate. This is dependent on the platform hardware.
   The actual rate may be determined by printing the QuadSPI object.

.. method:: QuadSPI.deinit()

   Turn off the QuadSPI bus.

.. method:: QuadSPI.read(nbytes)

    Read a number of bytes specified by ``nbytes``.
    Returns a ``bytes`` object with the data that was read.

.. method:: QuadSPI.readinto(buf)

    Read into the buffer specified by ``buf``.
    Returns ``None``.

.. method:: QuadSPI.write(buf)

    Write the bytes contained in ``buf``.
    Returns ``None``.

Constants
---------

.. data:: QuadSPI.MSB

   set the first bit to be the most significant bit
