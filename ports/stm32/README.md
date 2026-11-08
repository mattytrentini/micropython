MicroPython port to STM32 MCUs
==============================

This directory contains the port of MicroPython to ST's line of STM32
microcontrollers.  Supported MCU series are: STM32C5, STM32F0, STM32F4, STM32F7,
STM32G0, STM32G4, STM32H5, STM32H7, STM32L0, STM32L1, STM32L4, STM32WL and STM32WB.
Parts of the code here utilise the STM32Cube HAL library.

The officially supported boards are the line of pyboards: PYBv1.0 and PYBv1.1
(both with STM32F405), PYBLITEv1.0 (with STM32F411) and PYBD-SFx (with
STM32F7xx MCUs).  See
[micropython.org/pyboard](http://www.micropython.org/pyboard/) for further
details.

Other boards that are supported include ST Discovery and Nucleo boards.
See the boards/ subdirectory, which contains the configuration files used
to build each individual board.

Build instructions
------------------

Before building the firmware for a given board the MicroPython cross-compiler
must be built; it will be used to pre-compile some of the built-in scripts to
bytecode.  The cross-compiler is built and run on the host machine, using:
```bash
$ make -C mpy-cross
```
This command should be executed from the root directory of this repository.
All other commands below should be executed from the ports/stm32/ directory.

An ARM compiler is required for the build, along with the associated binary
utilities.  The default compiler is `arm-none-eabi-gcc`, which is available for
Arch Linux via the package `arm-none-eabi-gcc`, for Ubuntu via instructions
[here](https://launchpad.net/~team-gcc-arm-embedded/+archive/ubuntu/ppa), or
see [here](https://launchpad.net/gcc-arm-embedded) for the main GCC ARM
Embedded page.  The compiler can be changed using the `CROSS_COMPILE` variable
when invoking `make`.

Next, the board to build must be selected.  The default board is PYBV10 but any
of the names of the subdirectories in the `boards/` directory is a valid board.
The board name must be passed as the argument to `BOARD=` when invoking `make`.

All boards require certain submodules to be obtained before they can be built.
The correct set of submodules can be initialised using (with `PYBV11` as an
example of the selected board):

    $ make BOARD=PYBV11 submodules

Then to build the board's firmware run:

    $ make BOARD=PYBV11

The above command should produce binary images in the `build-PYBV11/`
subdirectory (or the equivalent directory for the board specified).

Note that some boards require the mboot bootloader to be built and deployed
before flashing the main firmware.  For such boards an information message
about this will be printed at the end of the main firmware build.  Mboot
can be built via:

    $ make -C mboot BOARD=STM32F769DISC

For more information about mboot see mboot/README.md.

The `SEEED_XIAO_STM32C5` board uses its factory TinyUF2 bootloader instead of
mboot. Its application starts at `0x08008000`, preserving the first 32 KiB.
Convert the built image to UF2 with:

    $ python ../../tools/uf2conv.py -f 0x00c5c5c5 -c -o build-SEEED_XIAO_STM32C5/firmware.uf2 build-SEEED_XIAO_STM32C5/firmware.hex

Double-press Reset to mount `XIAOC5BOOT`, then copy `firmware.uf2` to that
drive. From a working XIAO REPL, `machine.bootloader()` requests the same
factory TinyUF2 mode without pressing Reset. It writes TinyUF2's double-reset
marker (`0xf01669ef`) to `0x2003fffc` and performs a system reset; it neither
enters the STM32 ROM bootloader nor replaces the factory bootloader. Physical
Reset is still needed if the application or USB REPL is unresponsive.
Do not flash this board's application at `0x08000000`.

The STM32C5A3xG silicon revision Z (DBGMCU_IDCODE revision ID `0x1001`) has
an LSE low-drive limitation (ST errata ES0677, section 2.2.3): `LSEDRV=00`
may prevent the LSE crystal from starting or cause it to stop. These boards
select medium-high drive as specified by their board configuration. RM0522
revision 1, page 319, permits changing `LSEDRV` only while both `LSEON` and
`LSERDY` are clear. Firmware configures the drive before enabling an
unconfigured LSE and never changes an established RTC source. If an earlier
firmware leaves low drive selected while `LSERDY` remains set, a CPU reset or
firmware update cannot repair the retained RTC-domain state. Recovery requires
an RTC-domain power reset or `RTCDRST`; either resets RTC/TAMP/backup data and
must be explicitly authorized before use.

See [ES0677](https://www.st.com/resource/en/errata_sheet/es0677-stm32c5a3xg-stm32c593xx-and-stm32c591xx-device-errata-stmicroelectronics.pdf)
and [RM0522](https://www.st.com/resource/en/reference_manual/rm0522-stm32c5-series-armbased-32bit-mcus-stmicroelectronics.pdf).

### Link Time Optimization

Link Time Optimization (LTO) reduces the firmware binary size when enabled
(typically 2-3% smaller). However it may make build time longer, particularly on
older GCC versions.

Currently LTO is enabled by default for some smaller STM32 boards with less
flash, but disabled on other boards.

To enable LTO, pass `LTO=1` on the command line:

    $ make BOARD=boardname LTO=1

To disable LTO, pass `LTO=0` in the same way.

Note that `make clean BOARD=boardname` will be needed before changing the `LTO`
setting of a firmware that is already built.

### Flashing the Firmware using DFU mode

You must then get your board/microcontroller into DFU (Device Firmware
Update) mode.

If you already have MicroPython installed on the board you can do that by
calling `machine.bootloader()` on the board, either at the REPL or using
`pyboard.py`. A nice property of this approach is that you can automate it
so you can update the board without manually pressing any buttons.

If you do not have MicroPython running yet, temporarily jumper your board's
DFU pin (typ. BOOT0) to 3.3v and reset or power-on the board.

On a pyboard the P1/DFU pin and a 3.3v pin are next to each other (on the
bottom left of the board, second row from the bottom) and the reset button
is labeled RST.

For the pyboard D-series you can enter the mboot DFU bootloader by holding down
the USR button, pressing and releasing the RST button, and continuing to hold
down USR until the LED is white (4th in the cycle), then let go of USR while
the LED is white. The LED will then flash red once per second to indicate it
is in USB DFU mode.

Once the board is in DFU mode, flash the firmware using the command:

    $ make BOARD=PYBV11 deploy

This will use the included `tools/pydfu.py` script.  You can use instead the
`dfu-util` program (available [here](http://dfu-util.sourceforge.net/)) by
passing `USE_PYDFU=0`:

    $ make BOARD=PYBV11 USE_PYDFU=0 deploy

If flashing the firmware does not work it may be because you don't have the
correct permissions.  Try then:

    $ sudo make BOARD=PYBV11 deploy

Or using `dfu-util` directly:

    $ sudo dfu-util -a 0 -d 0483:df11 -D build-PYBV11/firmware.dfu


### Flashing the Firmware with stlink

ST Discovery or Nucleo boards have a builtin programmer called ST-LINK. With
these boards and using Linux or OS X, you have the option to upload the
`stm32` firmware using the `st-flash` utility from the
[stlink](https://github.com/texane/stlink) project. To do so, connect the board
with a mini USB cable to its ST-LINK USB port and then use the make target
`deploy-stlink`. For example, if you have the STM32F4DISCOVERY board, you can
run:

    $ make BOARD=STM32F4DISC deploy-stlink

The `st-flash` program should detect the USB connection to the board
automatically. If not, run `lsusb` to determine its USB bus and device number
and set the `STLINK_DEVICE` environment variable accordingly, using the format
`<USB_BUS>:<USB_ADDR>`. Example:

    $ lsusb
    [...]
    Bus 002 Device 035: ID 0483:3748 STMicroelectronics ST-LINK/V2
    $ export STLINK_DEVICE="002:0035"
    $ make BOARD=STM32F4DISC deploy-stlink


### Flashing the Firmware with OpenOCD

Another option to deploy the firmware on ST Discovery or Nucleo boards with a
ST-LINK interface uses [OpenOCD](http://openocd.org/). Connect the board with
a mini USB cable to its ST-LINK USB port and then use the make target
`deploy-openocd`. For example, if you have the STM32F4DISCOVERY board:

    $ make BOARD=STM32F4DISC deploy-openocd

The `openocd` program, which writes the firmware to the target board's flash,
is configured via the file `ports/stm32/boards/openocd_stm32f4.cfg`. This
configuration should work for all boards based on a STM32F4xx MCU with a
ST-LINKv2 interface. You can override the path to this configuration by setting
`OPENOCD_CONFIG` in your Makefile or on the command line.

Accessing the board
-------------------

Once built and deployed, access the MicroPython REPL (the Python prompt) via USB
serial or UART, depending on the board.  There are many ways to do this, one of
which is via `mpremote` (install it using `pip install mpremote`):

    $ mpremote

Other options are `picocom` and `screen`, for example:

    $ picocom /dev/ttyACM0

On `NUCLEO_C5A3ZG`, the ST-LINK virtual COM port provides the UART REPL
(USART2 on PA2/PA3, 115200 baud). The User USB connector provides a separate
USB CDC REPL. On `SEEED_XIAO_STM32C5`, use the USB CDC port after flashing;
`XIAOC5BOOT` is the bootloader drive, not the running MicroPython console.
On `WEACTSTUDIO_STM32C552`, use native USB CDC or USART1 on the P5 TX/RX header
(PA9/PA10, 115200 baud). This board uses the STM32 ROM DFU bootloader, not UF2;
see its [deployment instructions](boards/WEACTSTUDIO_STM32C552/deploy.md).

STM32C5 peripherals
-------------------

The C5 `stm32lib` dependency must integrate the new vendor import with the
existing MicroPython HAL and CMSIS fixes. Using the raw vendor-import branch
alone drops fixes required by other STM32 boards. C5-specific backends are
selected only for C5 builds; other families retain their existing drivers and
register layouts.

External GPIO interrupts are available through `machine.Pin.irq()` and
`pyb.ExtInt`, with rising/falling edges and hard or scheduled callbacks.
Only one GPIO port can own each numbered EXTI line at a time. Other non-GPIO
sources are not exposed through `pyb.ExtInt`.

`machine.ADC` supports C5 single-ended 12-bit conversions, scaled to the full
0–65535 range by `read_u16()`. Construct with a `Pin`, or with an ADC1 channel
number (0–11). `ADC.CORE_VREF` and `ADC.CORE_TEMP` select the internal reference
and temperature-sensor inputs; numeric channels 13 and 12 are their aliases.
`ADC.VREF` is the synthetic full-scale channel, not a voltage measurement.
There is no `read_uv()`, VBAT channel, attenuation or configurable sample time.
External inputs use 25 ADC cycles and internal inputs 289 cycles.
Calibration, regulator startup and conversions have bounded waits and raise
`OSError` on failure rather than returning stale conversion data.
Pin routing selects ADC1, ADC2 or ADC3 from generated metadata; PB0/PB1/PB2
prefer ADC2 because their alternative ADC3 channel numbers differ.
The C5A3 routing follows the
[STM32C5A3ZG chip data](https://github.com/embassy-rs/stm32-data-generated/blob/main/data/chips/STM32C5A3ZG.json).
`ADC.deinit()` disables that ADC instance without resetting the shared ADC12
block. Other live handles re-enable it on their next read. Soft reset resets
and gates the ADC instances without disturbing DAC1.

`machine.DAC(1)` or `machine.DAC(Pin.cpu.A4)` selects C5's single external
DAC1 channel. `write(value)` accepts unsigned 12-bit values (0–4095); invalid
values raise `ValueError` without changing the output. The output buffer is
enabled. Constructors alias the same peripheral and preserve its output while
active. `deinit()` disables the output and its dedicated bus gate; writes then
raise `OSError(ENODEV)`. Reconstruction restarts at zero. Waveform generation,
timed/DMA output, and the legacy `pyb.ADC`/`pyb.DAC` APIs are not enabled.
**Do not use DAC on an unmodified XIAO: PA4 is its battery-sensing input,
not an unconnected expansion pin.**

Both converters use the shared PSIS / 8 (18 MHz) analog kernel clock. The mux
is never changed underneath an enabled analog consumer. Active ADC or DAC
instances block system-clock changes and STOP; deinitialize them first.
Host register-model smoke checks exercise the production backends, but do not
establish electrical accuracy or silicon timing. Hardware checks are prepared
in `tests/ports/stm32/machine_adc_c5.py` (internal channels, no fixture),
`machine_adc_c5_pins.py` (opt-in known-voltage inputs), and
`machine_dac_c5.py` (Nucleo/WeAct, opt-in PA4-unconnected confirmation and
same-pad ADC readback). See the test headers for fixture module names and
contents. XIAO checks cover internal conversions at 48/144 MHz, IRQ-masked
reads, clock-change guards, ADC1/ADC2 input-only routing, shared-handle
restart, soft-reset cleanup and 1,000 construct/deinit cycles with stable
collected heap. Known external voltages have not yet been tested.
The XIAO's factory VREFINT calibration data produces an estimated analog
reference supply of about 3.75 V, inconsistent with the board's nominal
3.3 V rail. The cause is unresolved; verify the supply with a meter and use
known-voltage inputs before relying on absolute voltage accuracy. No guessed
gain or factory-calibration correction is applied.
DAC output accuracy, loading/settling and low-power transitions still need
physical validation. On the XIAO, only DAC argument rejection is tested;
PA4 is not driven and the DAC bus gate remains disabled.

`pyb.RTC` exposes calendar initialization and `datetime()` read/write on the
C5 boards. The XIAO, Nucleo and WeAct hardware provide a 32.768 kHz LSE crystal.
When RTCSEL is unset, startup uses the board-configured oscillator drive and
selects the board-configured source with the one-time initial selector write.
The C5 LSE drive matches Zephyr's `clk_lse` default (`driving-capability = <2>`)
in `dts/arm/st/c5/stm32c5.dtsi`; `dts/bindings/clock/st,stm32-lse-clock.yaml`
defines the range and value, and the XIAO board enables `clk_lse` in
`boards/seeed/xiao_stm32c5/xiao_stm32c5.dts`.
Startup adopts an existing RTC clock source; it never resets the RTC backup
domain or changes an already-selected source. A running LSE oscillator is also
preserved. The calendar is initialized to 2000-01-01 only when the RTC reports
that its calendar has never been initialized. Existing calendar contents and
TAMP backup registers are preserved. The Python `time` module's calendar and
wall-clock functions use this RTC; `time.time()` and `time.time_ns()` use the
MicroPython epoch of 2000. `pyb.RTC.wakeup()` uses the RTC wakeup timer and
schedules callbacks through the normal deferred callback path. It requires a
supported LSE/LSI clock and its standard prescalers; calibration remains
unavailable.
RTC operations and wakeup interrupt acknowledgement preserve the previous
RTC-domain write protection state, so backup-memory views remain writable.
Disabling wakeup uses `rtc.wakeup(None)`.
A running calendar is preserved when its year is 2000, despite the hardware
INITS flag being clear for a zero year field. RTC reinitialization and Standby
wake must not replace that calendar with the startup default.

`pyb.Timer` supports counter/update callbacks, PWM and output compare,
input capture, encoder modes, and advanced complementary outputs/deadtime.
Callbacks may be hard or scheduled. TIM2/3/4/5 have 32-bit counters where
present; TIM6/7 support basic counter/update operation only. C5 timer source
clocks use HCLK.

`machine.PWM` uses the shared STM32 driver for main timer-channel outputs,
with `freq`, `duty_u16`, `duty_ns`, and `invert` support. Channels on the same
timer share a frequency: changes preserve configured duty ratios or pulse
widths, but do not restart explicitly deinitialized siblings. An explicit
`init`, frequency or duty update can restart a deinitialized output.
Complementary outputs remain available through `pyb.Timer`, not `machine.PWM`.
Do not mix the two APIs on the same timer. Stop PWM before changing the
system clock or entering STOP; the existing active-timer guards apply.
`tests/ports/stm32/pwm_c5.py` measures physical output pads without jumpers.
Leave PA0, PA1 and PA10 unconnected and run over USB; the test temporarily
deinitializes UART1 where PA10 is its RX pin and restores it afterward.

`machine.SPI` provides polling master transfers on configured board buses.
The current backend supports 8- and 16-bit frames, all CPOL/CPHA modes, and
software-managed transfer lengths beyond the hardware counter limit. The XIAO
SPI3 routes D8/D9/D10 to PE2/PB0/PB15; the Nucleo uses PB3/PB4/PB5. These
alternate functions were checked against ST DS15137 Table 14. DMA and the
legacy `pyb.SPI` API remain disabled on C5.
The SPI hardware regression covers both boards using physical MISO pull levels,
including FIFO boundaries, 8-/16-bit frames, long transfers and error recovery.
Leave the expansion SPI pins unconnected when running
`tests/ports/stm32/spi_c5.py`.

`machine.I2S` supports I2S1 master transmit and receive on the Nucleo and WeAct
boards. Pass clock, word-select and data pins explicitly; PA4/PA5/PA6/PA7 and
PB3/PB4/PB5 provide these alternate functions. On the Nucleo, PA5 is also the
on-board LED. PB3/PB4/PB5 are used by both boards' SPI3 configurations.
Transfer-rate tests cover 16-/32-bit mono and stereo transmit and receive on
the Nucleo and WeAct. External hardware is still required to validate audio
sample contents; no audio loopback is wired on either board.
Low-rate transmit configurations use the internal master-clock divider path
when PCLK2 exceeds the ordinary divider range. This supports 8 kHz at 144 MHz
without changing the wire-slot width or configuring an MCK output pin.

On C5 boards, `machine.freq()` reports the active SYSCLK, HCLK and APB clocks.
It supports direct HSE and 144 MHz PSIS SYSCLK with 1:1 bus dividers. HSE is
48 MHz on the Nucleo/XIAO and 8 MHz on the WeAct. USB-enabled builds reject
APB clocks below the TinyUSB FSDEV driver's 10 MHz minimum, so the WeAct's
USB firmware supports only the 144 MHz operating point. PSI and its PSIDIV3
USB clock remain unchanged. The CMSIS system-clock source is compiled with
the board HAL oscillator configuration so its HSE frequency remains available
when updating `SystemCoreClock`. A clock transition
is rejected with `OSError(EBUSY)` while a peripheral depending on the changed
clocks is enabled. Deinitialize those peripherals first; on the Nucleo this
includes the UART2 REPL.
The XIAO and WeAct default UART1 REPLs also block clock changes and STOP. When
testing clock transitions over USB, deinitialize `machine.UART(1)` first and restore
it at 115200 baud after returning to the board's default 144 MHz clock.

`machine.lightsleep([timeout_ms])` enters STOP0 and restores the prior system
clock, SysTick and PSI stop-clock setting after wake. RTC wakeup is used for a
timeout. Active non-REPL peripherals block STOP0; a connected USB device must
already be suspended by its host. The Nucleo UART2 REPL is allowed through STOP0
because its source clock is restored before interrupts run. `machine.deepsleep()`
enters Standby and resets on wake; an optional timeout uses the RTC wakeup timer.
A runtime USB device that has explicitly disconnected its D+ pull-up does not
block STOP0; an old TinyUSB connection flag alone does not imply an active bus.
STOP wake restores the previously enabled PSIS, PSIDIV3 and PSIK outputs;
restoring SYSCLK alone is insufficient because STOP clears the independent
PSIDIV3 enable used by USB. RTC-timed STOP and USB re-enumeration have been
checked on the Nucleo at both supported system-clock frequencies.

`machine.I2C` provides polling controller transfers on I2C1 and I2C2. The boards
use PB6/PB7 for I2C1. I2C2 uses PB3/PB4 on Nucleo/XIAO and connects to the
XIAO's onboard IMU; the WeAct uses PB10/PB12.
`machine.I2CTarget` is available on these hardware buses, with 7-bit and 10-bit
addresses, backing memory and IRQ callbacks. Target mode handles repeated STARTs
as a new transfer without reporting a STOP; connect a separate controller to the
board-defined target pins for transactions.
On the XIAO, I2C2 is shared with the onboard IMU at address 0x6A; a target on that
bus must not claim the IMU's address, and controller scans can still see the IMU.
The C5 target loopback uses I2C2 as controller and I2C1 as target; it requires
confirmed connections between the board-defined I2C2 and I2C1 SCL/SDA pins
before running.
`machine.mem_backup()` exposes the C5 TAMP backup-register bank as 32
four-byte words. The register bank is not cleared during MicroPython startup;
it retains values while the RTC backup domain remains powered.

The C5 flash backend uses the C5 HAL's polling program-by-address and page-erase
operations, including bank-swap-aware page selection. Programming uses the main
flash-bank geometry and 16-byte-aligned destinations. ICACHE is temporarily
disabled during flash mutations, invalidated afterward, and restored to its
previous state to prevent write intrusion and stale flash reads.
Flash programming delay is explicitly set for 144 MHz during startup rather
than inherited from a bootloader. Clock transitions select delay 0 at the
lower HSE frequency and delay 2 at 144 MHz, in clock-safe order.

The XIAO enables a FAT filesystem at `/flash` using the existing 256 KiB internal
flash reservation (`0x080c0000` to `0x08100000`) and an 8 KiB page cache. First boot
creates the filesystem and default startup files using the STM32 factory-reset
path. Files, imported modules, `boot.py` and `main.py` persist across resets and
application-only UF2 updates. The factory TinyUF2 bootloader occupies the first
32 KiB and is outside both the application and filesystem regions. The onboard
external SPI flash is not used for this filesystem.
The WeAct reserves 128 KiB at `0x08060000` to `0x08080000` for its FAT filesystem,
with an 8 KiB page cache. Firmware starts at `0x08000000` and is limited to the
preceding 384 KiB; application-only updates must leave the filesystem untouched.

USB remains CDC-only; use file operations or `mpremote` to transfer files.
Close files and call `os.sync()` before hard reset or removing power. This uses
the existing cached STM32 FAT backend, not a power-fail-safe flash filesystem.
Internal flash filesystem storage remains disabled on the Nucleo.

`machine.time_pulse_us()` uses the microsecond tick clock; timeouts are returned
as negative values as in the common machine API. `machine.bitstream()` uses the
Cortex-M cycle counter. CPU timing enables the trace clock independently of
the counter enable, recovering bootloader/debugger states that otherwise leave
the counter stopped. C5 microsecond delays also use this counter while IRQs
are disabled, rather than relying on an empty C delay loop.

The C5 board definitions freeze `neopixel.NeoPixel`. Automated pulse tests use
a timer-generated input signal; actual NeoPixel waveform and LED behaviour
still require external measurement/hardware.

`machine.CAN` exposes one configured Classical CAN bus on each board. The
backend uses the C5 FDCAN peripheral in Classical CAN mode, with a maximum
payload of eight bytes; CAN-FD and bit-rate switching are not enabled.
Standard/extended identifiers, RTR frames, acceptance filters, polling,
soft/hard callbacks and transmit cancellation are supported.
RX callbacks report newly stored frames. FIFO-full and lost-frame events latch
the receive error flags returned by `recv()`; they do not request extra RX
callbacks for frames that were not retained.
The hardware REC field is seven bits. While the separate receive-passive flag
is set, `get_counters()` reports REC as 128 (a lower bound), rather than
incorrectly reporting a value below the error-passive threshold.
TX queue slots remain reserved until their indexed completion notification
has been consumed. A slot becoming free in hardware alone does not permit
its reuse while the previous completion interrupt is pending.
The controlled two-node cancellation test keeps the peer silent until every
slot has been cancelled and reused, then restores ACKs and checks all indexed
completions. It does not require a babbler to saturate the bus.
Unpaced duplex traffic at 1 Mbit/s has produced an RX FIFO overrun with Python
hard callbacks. Check receive error flags and overrun counters; functional
support for this bitrate is not a guarantee of lossless Python-level throughput.

`CAN(1, 500_000)` selects FDCAN1 on PB9 (TX)/PB8 (RX) on the Nucleo, which
requires an external transceiver. On the XIAO, logical bus 1 selects FDCAN2 on
PB13 (TX)/PB5 (RX), connected to the onboard transceiver. XIAO PB14 is held
high in standby and driven low after successful CAN initialization; deinit
restores standby. External-bus operation requires appropriate wiring, common
ground and termination; internal silent loopback does not validate these.
The WeAct uses FDCAN1 on PD1 (TX)/PD0 (RX), with an external transceiver required.

CAN uses the board's independent HSE kernel clock. Deinitialize CAN before
changing the system clock or entering STOP. As with the existing STM32 CAN
backend, `MODE_SLEEP` selects bus monitoring, not hardware low-power sleep.
