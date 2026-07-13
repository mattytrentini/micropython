.. currentmodule:: machine
.. _machine.OctoSPI:

class OctoSPI -- an Octo SPI bus protocol (controller side)
==============================================================

OctoSPI is a variant of `SPI` that uses 8 data lines (``io0``-``io7``)
instead of separate MOSI/MISO lines, giving up to 8x the throughput of a
regular SPI bus of the same clock rate. It's commonly used for external
flash and PSRAM that support an octal data-line mode. `OctoSPI` requires
octal-capable SPI hardware, which is not available on every port or every
chip within a port's family.

`OctoSPI` has the same API as `QuadSPI`, differing only in having 8 data
lines instead of 4; see `machine.QuadSPI` for a full description of the
half-duplex read/write semantics (no ``write_readinto``, no fill-byte
parameter on ``read``/``readinto``) and the constructor/pin requirements.
As with `QuadSPI`, devices are usually addressed over regular single-line
`SPI` first to issue the mode-entry command, before switching to `OctoSPI`
for the data phase on the same pins.

Only hardware `OctoSPI` is currently supported.

Example usage::

    from machine import OctoSPI, SPI, Pin

    cs = Pin(5, mode=Pin.OUT, value=1)

    # Send the mode-entry command over regular 1-line SPI first.
    spi = SPI(1, baudrate=1000000, sck=Pin(18), mosi=Pin(19), miso=Pin(21))
    cs(0)
    spi.write(b"\x38")  # eg enter octal/OPI mode on an octal flash chip
    cs(1)
    spi.deinit()

    # Then switch to OctoSPI on the same pins (plus io2-io7).
    ospi = OctoSPI(1, baudrate=20000000, sck=Pin(18),
                   io0=Pin(19), io1=Pin(21), io2=Pin(22), io3=Pin(23),
                   io4=Pin(4), io5=Pin(6), io6=Pin(7), io7=Pin(8))
    cs(0)
    ospi.write(b"\x02\x00\x00\x00" + b"hello")
    cs(1)

Constructors
------------

.. class:: OctoSPI(id, ...)

   Construct an OctoSPI object on the given bus, *id*. Values of *id* depend
   on a particular port and its hardware.

   With no additional parameters, the OctoSPI object is created but not
   initialised (it has the settings from the last initialisation of
   the bus, if any).  If extra arguments are given, the bus is initialised.
   See ``init`` for parameters of initialisation.

Methods
-------

.. method:: OctoSPI.init(baudrate=1000000, *, polarity=0, phase=0, bits=8, firstbit=OctoSPI.MSB, sck=None, io0=None, io1=None, io2=None, io3=None, io4=None, io5=None, io6=None, io7=None)

   Initialise the OctoSPI bus with the given parameters:

     - ``baudrate`` is the SCK clock rate.
     - ``polarity`` can be 0 or 1, and is the level the idle clock line sits at.
     - ``phase`` can be 0 or 1 to sample data on the first or second clock edge
       respectively.
     - ``bits`` is the width in bits of each transfer. Only 8 is guaranteed to be supported by all hardware.
     - ``firstbit`` must be ``OctoSPI.MSB`` -- octal-mode hardware is generally
       MSB-first only.
     - ``sck``, ``io0``-``io7`` are `machine.Pin` objects to use for the bus
       signals. As with `QuadSPI`, there is usually no safe default pin set,
       so all 9 pins are typically required to be given explicitly, either
       here or as constructor arguments.

   In the case of hardware OctoSPI the actual clock frequency may be lower
   than the requested baudrate. This is dependent on the platform hardware.
   The actual rate may be determined by printing the OctoSPI object.

.. method:: OctoSPI.deinit()

   Turn off the OctoSPI bus.

.. method:: OctoSPI.read(nbytes)

    Read a number of bytes specified by ``nbytes``.
    Returns a ``bytes`` object with the data that was read.

.. method:: OctoSPI.readinto(buf)

    Read into the buffer specified by ``buf``.
    Returns ``None``.

.. method:: OctoSPI.write(buf)

    Write the bytes contained in ``buf``.
    Returns ``None``.

Constants
---------

.. data:: OctoSPI.MSB

   set the first bit to be the most significant bit
