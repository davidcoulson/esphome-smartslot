# Firmware as an ESPHome external component instead of a bare ESP-IDF app

Date: 2026-10-25. Answers "can we make all this an ESPHome component vs build from scratch".
Recommendation: yes. Evidence from this dashboard: ESPHome 2026.10.0 already runs the P4 node
`gps-ntp-p4` on the built-in EMAC + IP101 PHY over RMII with the exact pin map this card uses
(MDC 31, MDIO 52, PHY reset 51, REF_CLK in on 50, PHY addr 1), with IPv6 and LLDP. Everything the
from-scratch plan spent three tasks on (networking, OTA, console, config store, LED, web) is YAML.

## What changes against `esp32-p4-smartslot-firmware-plan.md`

| Plan task | From-scratch | ESPHome component |
| --- | --- | --- |
| 1 repo skeleton | IDF project, Kconfig, `test_apps/host` | repo `esphome-smartslot` with `components/smartslot/` (`__init__.py`, `smartslot.h/.cpp`, `nut/` vendored, `shim/`), host tests under `tests/` with plain gcc + Unity |
| 2 vendor NUT | same | same; C sources listed in `__init__.py` via `cg.add_build_flag`/`CORE.add_job` so PlatformIO compiles them, `extern "C"` wrappers |
| 3 nutshim | same | same |
| 4 serial layer | IDF `uart` driver | same IDF `uart` driver in the component's own FreeRTOS task (ESPHome's `uart:` is loop-driven and must not claim these pins) |
| 5 nutproto | same | same |
| 6 upsd TCP server | select() task on lwIP | same code, started from `setup()` |
| 7 nutdrv task | same | same; `dstate_foreach` also feeds ESPHome sensors on each poll |
| 8 cardcfg NVS + console | 6 console commands | deleted: `cmdsrc`, `baudrate`, `uartsel_pin`, `nut_user`/`nut_password`, static IP all come from YAML |
| 9 app_main wiring | LED, net, bench | `setup()`/`loop()`, `status_led:`, `ethernet:`; bench checklist stays |
| 10 Codex review, tag | same | same |

New, small: a `sensor`/`text_sensor`/`button` platform under `smartslot` exposing `ups.status`,
`battery.charge`, `battery.runtime`, `ups.load`, `input.voltage`, `output.voltage`, `battery.voltage`,
`ups.temperature`, and buttons for `beeper.mute`, `test.battery.start`, `test.panel.start`. Home Assistant
then needs no NUT integration; Unraid and other `upsmon` clients still use port 3493.

## Sketch of the YAML the user would write

```yaml
esp32:
  board: esp32-p4-evboard
  variant: esp32p4
  engineering_sample: true   # drop once the module is confirmed v3+ silicon
ethernet:
  type: IP101
  mdc_pin: GPIO31
  mdio_pin: GPIO52
  power_pin: GPIO51
  clk: { mode: CLK_EXT_IN, pin: GPIO50 }
  phy_addr: 1
external_components:
  - source: github://davidcoulson/esphome-smartslot@main
    components: [smartslot]
smartslot:
  tx_pin: GPIO20
  rx_pin: GPIO21
  uart_sel_pin: GPIO53
  cmdsrc: smartslot1
  nut:
    port: 3493
    username: !secret nut_user
    password: !secret nut_password
sensor:
  - platform: smartslot
    battery_charge: { name: Battery charge }
    load: { name: Load }
    runtime: { name: Runtime }
text_sensor:
  - platform: smartslot
    status: { name: UPS status }
button:
  - platform: smartslot
    beeper_mute: { name: Mute beeper }
```

## Trade-offs

- Gains: OTA, web UI, API, logger, static IP, IPv6, mDNS-free Ethernet, the user's existing fleet tooling
  (`common/djc-common.yaml`), and HA entities with no extra integration. Roughly 40 % less new code.
- Costs: P4 builds in ESPHome take minutes per compile; the `engineering_sample` flag must match the module's
  silicon; GPL-2+ vendored NUT code lives in its own repo (fine for an external component, as `esphome-pps-ntp`
  already does for other code). The NUT server still has no TLS (same as the from-scratch plan).
- Unchanged: the vendored `apcmicrolink.c` stays byte-identical; the probe, UART_SEL, 4-client limit and
  reply texts from the plan's Global Constraints carry over.
