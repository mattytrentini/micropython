The following files are firmware for the Waveshare ESP32-P4-Nano.

This board has an onboard ESP32-C6-MINI-1 module that provides WiFi and Bluetooth
connectivity to the ESP32-P4 over SDIO, via the ESP-Hosted interface.

It also has an onboard IP101 Ethernet PHY, connected via RMII. See
:ref:`esp32_network_lan` in the quick reference for the pin configuration
required to use it.

The two 2x13 GPIO headers (P1, P2) bring out 28 programmable GPIOs, accessible
via ``machine.Pin`` using their CPU pin names (e.g. ``machine.Pin("GPIO7")`` or
``machine.Pin(7)``) -- there are no board-specific pin aliases::

    P1: 1=3V3   2=PWR  3=GPIO7  4=PWR    5=GPIO8  6=GND    7=GPIO23  8=GPIO37
        9=GND  10=GPIO38 11=GPIO5 12=GPIO4 13=GPIO20 14=GND  15=GPIO21 16=GPIO22
       17=3V3  18=GPIO24 19=GPIO25 20=GND  21=GPIO26 22=GPIO27 23=GPIO32 24=GPIO33
       25=GND  26=GPIO36

    P2:  1=PWR   2=PWR   3=GND    4=GND    5=3V3    6=GPIO0  7=GND    8=GPIO1
         9=GPIO3 10=GND  11=GPIO2 12=GPIO6 13=GPIO54 14=GPIO53 15=GPIO47 16=GPIO48
        17=GPIO46 18=GND 19=GPIO45 20=C6_U0RXD 21=C6_IO12 22=C6_U0TXD 23=C6_IO13
        24=C6_IO9 25=GND 26=GND

Notes:

- GPIO37 and GPIO38 are shared with the onboard CH343 USB-UART bridge used for
  the REPL; reconfiguring them will disrupt the serial console.
- GPIO24 and GPIO25 (P1 pins 18 and 19) are reserved by the ESP32-P4's
  USB-Serial-JTAG peripheral in this board's default configuration (even
  though that peripheral isn't used here -- the REPL uses the CH343 bridge
  instead) and are not available via ``machine.Pin``.
- P2 pins 20-24 (labelled ``C6_*``) are signals of the onboard ESP32-C6
  co-processor, not ESP32-P4 GPIOs, and are not accessible via ``machine.Pin``
  on this port.
