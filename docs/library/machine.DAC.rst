.. currentmodule:: machine
.. _machine.DAC:

class DAC -- digital to analog conversion
=========================================

The DAC is used to output an analog voltage based on a digital value.

The output voltage range follows the analog reference supply, usually 0 to 3.3V.

DAC is currently supported on ESP32 [#esp32_dac]_, SAMD, Renesas RA and STM32C5.

.. note::
  Other STM32 families expose DAC functionality through
  :ref:`pyb.DAC <pyb.DAC>`.

Example usage (ESP32)::

    from machine import DAC

    dac = DAC(pin)    # create a DAC object acting on a pin
    dac.write(128)    # write a value to the DAC
    dac.write(255)    # output maximum value, 3.3V

Constructors
------------

.. class:: DAC(id)

  Construct a new DAC object.

  ``id`` is a pin object (ESP32, Renesas RA and STM32C5) or an index to a DAC
  resource (SAMD and STM32C5).

.. note::
  On the ESP32, DAC functionality is available on pins 25 and 26. On the
  ESP32-S2, pins 17 and 18. See :ref:`ESP32 Quickref <esp32_quickref>`
  for more details.

.. note::
  SAMD21 has one DAC resource, SAMD51 has two. See :ref:`SAMD Quickref <samd_quickref>`
  for more details.

.. note::
  On STM32C5, ``DAC(1)`` and ``DAC(Pin.cpu.A4)`` select the same buffered,
  12-bit output. Repeated construction preserves the active output. Its voltage
  range follows the board's analog reference supply.

.. warning::
  The XIAO STM32C5 connects PA4 to its battery-sensing circuit. Do not enable
  DAC on an unmodified XIAO board.


Methods
-------

.. method:: DAC.write(value)

  Output an analog voltage to the pin connected to the DAC.

  ``value`` is a representation of the desired output; a linear interpolation of
  the analog reference range. The numeric range differs depending on the port
  and micro, see below:

  +--------------+------+--------+
  | *Port/micro* | Bits | Range  |
  +==============+======+========+
  | ESP32        | 8    | 0-255  |
  +--------------+------+--------+
  | SAMD21       | 10   | 0-1023 |
  +--------------+------+--------+
  | SAMD51       | 12   | 0-4095 |
  +--------------+------+--------+
  | Renesas RA   | 12   | 0-4095 |
  +--------------+------+--------+
  | STM32C5      | 12   | 0-4095 |
  +--------------+------+--------+

.. method:: DAC.deinit()

  On STM32C5, disable the output and release its dedicated peripheral bus clock.
  Subsequent writes raise ``OSError(ENODEV)``. Constructing the DAC again
  restarts its output at zero.

.. rubric:: Footnotes

.. [#esp32_dac] The original ESP32 and ESP32-S2 *only*, since DAC hardware is
    not present on other microcontrollers in the family.
