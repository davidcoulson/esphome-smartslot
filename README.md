# esphome-smartslot

An ESPHome external component that turns an ESP32-P4 board into an open replacement for the APC SmartSlot network
card (AP9640/AP9641) for Microlink Smart-UPS models (SMT750, SMX3000). It runs NUT's `apcmicrolink` driver
unmodified against the UPS serial port and serves a NUT `upsd` on TCP 3493, plus ESPHome sensors, text sensors and
buttons for Home Assistant.

Status: firmware complete on the host (unit tests) and compiles with ESPHome 2026.10.0; not yet bench-tested on a UPS.
Design: `docs/esp32-p4-smartslot-design.md`. Plan: `docs/esp32-p4-smartslot-firmware-plan.md`. Notes: `docs/dev-notes.md`.

Licence: GPL-2.0-or-later (the vendored NUT driver is GPL-2.0-or-later; Unity under `tests/unity` is MIT).

## Use

```yaml
external_components:
  - source: github://davidcoulson/esphome-smartslot@main
    components: [smartslot]

ethernet:            # stock ESPHome, IP101 PHY on the ESP32-P4 EMAC
  type: IP101
  mdc_pin: GPIO31
  mdio_pin: GPIO52
  power_pin: GPIO51
  clk: { mode: CLK_EXT_IN, pin: GPIO50 }
  phy_addr: 1

smartslot:
  tx_pin: GPIO20          # defaults shown
  rx_pin: GPIO21
  uart_sel_pin: GPIO53
  uart_sel_level: 0
  uart_num: 1
  cmdsrc: smartslot1      # rj45 / db9 for a bench cable on the front port
  baud_rate: 9600
  update_interval: 10s    # how often entities are published
  nut:
    port: 3493
    username: !secret nut_user     # empty = INSTCMD/SET/LOGIN refused
    password: !secret nut_password

sensor:
  - platform: smartslot
    variable: battery.charge
    name: Battery charge
text_sensor:
  - platform: smartslot
    variable: ups.status
    name: UPS status
button:
  - platform: smartslot
    command: beeper.toggle
    name: Toggle beeper
```

`variable` is any NUT variable name (`upsc ups@card` lists them); `command` is any NUT instant command
(`upscmd -l`). Entities stop publishing while the UPS is unreachable or data is stale, and the component reports a
warning status (drive a `status_led:` from it). Reads on port 3493 need no login; `INSTCMD`, `SET VAR` and `LOGIN`
need the configured user. There is no TLS, SNMP, Wi-Fi or mDNS in the component.

## Layout

- `components/smartslot/` ESPHome glue (`*.py`, `smartslot*.cpp/h`) and `CMakeLists.txt`, which ESP-IDF builds
  together with `nut/` (vendored driver, byte-identical, see `nut/SHA256SUMS`), `shim/` (NUT API on ESP-IDF) and
  `upsd/` (protocol and server).
- `tests/` host tests: `make -C tests` (gcc + Unity).
- `example/smartslot-bench.yaml` bench config; `esphome compile example/smartslot-bench.yaml`.

## Entities

```yaml
sensor:
  - platform: smartslot
    variable: battery.charge      # any NUT variable name
    name: Battery charge
text_sensor:
  - platform: smartslot
    variable: ups.status
    name: UPS status
button:
  - platform: smartslot
    command: beeper.toggle        # any NUT instant command
    name: Toggle beeper
```

Nothing is published while the UPS is absent or its data is stale. `nutshim_value_for_publish` lives in `nutdrv.h`.
