# ESP32 `machine.CAN` (TWAI) — implementation notes

Status: implements the shared `extmod/machine_can_port.h` interface using the
ESP-IDF **node-based TWAI driver** (`driver/twai_onchip.h`), and is
**hardware-validated** on a LilyGo T-2CAN (ESP32-S3, ESP-IDF v5.5.2):

- `MODE_SILENT_LOOPBACK`: init/timing, `send`/`recv` of standard and extended
  frames, `set_filters`, `state`, `get_counters`, `get_timings`, `deinit`.
- **`MODE_NORMAL` on a live two-node bus** — the T-2CAN's CAN-A (MCP2515 over
  SPI, `jxltom/micropython-mcp2515`) wired to CAN-B (native TWAI) at 500 kbit/s:
  - standard and extended frames cross both directions;
  - `irq()` fires from real ISR callbacks, and `IRQ_TX` reports the **accurate**
    transmit-buffer index for each completion individually;
  - single mask filter matches by ID with correct std/ext discrimination
    (an extended frame is rejected by a standard-ID filter);
  - TWAI stays ACTIVE with TEC/REC = 0 (every frame ACKed by the other node).

Integration notes (things the port must get right):
- The CAN `id` is **1-based**: `CAN(1, ...)` is the first controller.
- `machine_can_deinit_all()` is provided by `extmod/machine_can.c` (declared in
  `extmod/machine_can.h`); the port must **not** define it.
- The port/board must define `MICROPY_HW_CAN_IS_RESERVED(can_id)` (default
  `false`); done in `mpconfigport.h`.

## Architecture

The standardised `machine.CAN` interface (merged in micropython/micropython
PR #18572) splits into:

- `extmod/machine_can.c` — the Python-facing type, argument parsing, bit-timing
  maths and IRQ dispatch. Shared by all ports; do not modify here.
- A port file providing the static `machine_can_port_*()` functions declared in
  `extmod/machine_can_port.h`. This file (`ports/esp32/machine_can.c`) is pulled
  in textually via `MICROPY_PY_MACHINE_CAN_INCLUDEFILE`.

## Wiring

- `ports/esp32/mpconfigport.h` — enables `MICROPY_PY_MACHINE_CAN`
  (`SOC_TWAI_SUPPORTED && ESP_IDF_VERSION >= 5.4.0`), sets the include file and a
  default `MICROPY_HW_NUM_CAN` of 1 and `MICROPY_HW_CAN_IS_RESERVED`.
- `ports/esp32/modmachine.h` / `main.c` — declares and calls
  `machine_can_deinit_all()` on soft reset.
- No esp32 CMake source edit is needed: `extmod/machine_can.c` is already built
  and registers `machine.CAN`, and the port file is `#include`d. The node API
  lives in the `esp_driver_twai` component, pulled in via the `driver`
  meta-component already in the build.

## Driver: node-based `driver/twai_onchip.h` (ESP-IDF >= 5.4)

`machine.CAN` is gated on ESP-IDF v5.4 because the node driver (`esp_driver_twai`)
was introduced then. MicroPython's esp32 port also supports v5.3, where CAN is
simply unavailable. The node driver is the maintained API and maps cleanly onto
`machine.CAN`; the older `driver/twai.h` (polling `twai_read_alerts()`, single
global controller) was rejected for this reason.

Design:
- **Events are ISR callbacks** (`on_rx_done`, `on_tx_done`, `on_state_change`)
  registered with `twai_node_register_event_callbacks`. No polling task.
- **RX**: `on_rx_done` calls `twai_node_receive_from_isr` and pushes into a
  software ring; `machine_can_port_recv()` pops from it. (The node driver only
  delivers RX via the ISR, so the ring is required even for polled use.)
- **TX**: a FIFO ring of persistent slots (`CAN_TX_QUEUE_LEN`). `send()` fills the
  next slot, calls `twai_node_transmit` (which keeps the frame pointer until
  done, so the slot must persist), and returns the slot index. Completions are
  FIFO, so `on_tx_done` reports the oldest in-flight index. Each completion is
  queued and returned individually by `machine_can_port_irq_flags()` (one event
  per `flags()` call) so distinct TX indexes are never coalesced.
- **Timing**: `CAN_USE_UPSTREAM_TIMING` is set; extmod computes
  `brp`/`tseg1`/`tseg2`/`sjw` from `bitrate` + `sample_point` and the port applies
  them via `twai_node_reconfig_timing` (advanced timing). `brp` bounds come from
  `SOC_TWAI_BRP_MIN/MAX`. This avoids the `TWAI_TIMING_CONFIG_*KBITS()` macros
  that caused timing bugs in the historical ESP32 CAN PRs.
- **Filters**: one mask filter (`SOC_TWAI_MASK_FILTER_NUM == 1` on classic
  controllers). The node mask uses 1=match (same as `machine.CAN`), so `id`/`mask`
  map directly with no shifting/inversion, and the driver does software std/ext
  discrimination via `is_ext`. Accept-all = `{id:0, mask:0}` (full-open, both
  std+ext), accept-none = `{0xFFFFFFFF, 0xFFFFFFFF}` (full-close). Filter config
  requires the node stopped, so `set_filters` at runtime disables/re-enables it.
- **Modes**: `twai_onchip_node_config_t.flags` — SILENT = listen-only;
  LOOPBACK / SILENT_LOOPBACK = loopback + self-test (no ACK).

## Remaining limitations (hardware, not bugs — kept documented)

These are inherent to the classic ESP32/S3 TWAI controller and `machine.CAN`
explicitly tolerates them; do not "implement" them:

1. **Single filter** — `FILTERS_MAX` is 1. More than one filter tuple raises
   `ValueError`.
2. **No RTR filtering** — the node mask filter has no RTR field, so a filter with
   `FLAG_RTR` raises `ValueError` ("RTR filtering not supported"), per the
   "unsupported flag" clause in the `set_filters` docs.
3. **`cancel_send()` returns `False`** — the driver exposes no per-message
   cancellation. TX is FIFO (not priority); `FLAG_UNORDERED` is a no-op.
4. **LOOPBACK vs SILENT_LOOPBACK** are indistinguishable (both loopback+self-test).
   `MODE_SLEEP` is unsupported.
5. **Single controller** — `SOC_TWAI_CONTROLLER_NUM == 1` on ESP32-S3, so there's
   one `CAN(1)`. Multi-controller chips would need a per-`id` pin/handle table.
6. **`f_clock`** is 80 MHz (APB) — correct for ESP32/S3; confirm for C3/C6/H2.

## Build

```sh
mpbuild build LILYGO_T2CAN
# or: cd ports/esp32 && idf.py -D MICROPY_BOARD=LILYGO_T2CAN build
```

`machine.CAN` is enabled automatically on any TWAI-capable target built with
ESP-IDF >= 5.4.

## Test plan

- `tests/multi_extmod/machine_can_*.py` are the standard two-instance CAN tests;
  they need two `machine.CAN` nodes on a shared bus. Tests `03` (multi-filter),
  `04`/`05` (TX priority/cancel) exercise features unsupported by this hardware
  and are expected to fail by design.
- For a single T-2CAN, `MODE_SILENT_LOOPBACK` exercises tx→rx without a bus, and
  the CAN-A MCP2515 can serve as the second node for a live `MODE_NORMAL` test.
