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
need the configured user. There is no TLS, SNMP, Wi-Fi or mDNS in the component; see "Web interface" below for the
built-in web UI.

## Web interface

The example config serves ESPHome's built-in `web_server` (version 3) on `http://<card-ip>/` (port 80), with the UI
assets stored in flash (`local: true`) so it works without internet access. It asks for the `web_user` /
`web_password` from `example/secrets.yaml` (copy `secrets.yaml.example` and change them). The page shows, in groups:

- Status: UPS status, UPS temperature, uptime, Ethernet link, IP and MAC address, a count of stale polls (update
  cycles where the driver was down or the UPS data stale), the beeper toggle, and the live log stream.
- Battery: charge, runtime, voltage. Input/Output: input and output voltage, load.
- Settings, stored in flash and kept across reboots: poll interval (applied immediately), SNMP community, sysName,
  sysLocation, sysContact (stored only until the `snmp_agent` component lands), and a reboot button. Firmware can
  be uploaded from the page too (`ota: platform: web_server`). The UART pins, UART_SEL level and buffer bypass are
  board jumpers read at boot, so they have no runtime setting.

The server is plain HTTP, no TLS. Digest auth keeps the password itself off the wire, but every reading, setting
(including the SNMP community) and firmware upload crosses the LAN in clear and can be replayed, so keep the card on
a trusted management VLAN and do not expose port 80 beyond it.

## History on flash

`smartslot: history:` keeps a long-term record of the UPS readings on the card itself, charted at
`http://<card-ip>/history` (same login as the web UI; bookmark it, the stock `local: true` page cannot carry a link) and as JSON at
`/history.json?from=<epoch>&to=<epoch>`. It stores battery charge, runtime, input and output voltage, load,
temperature and the share of each interval spent on battery in [esp_tsdb](https://github.com/zakery292/esp_tsdb) on a
LittleFS partition, the way [esphome-tigomonitor](https://github.com/RAR/esphome-tigomonitor) does (both MIT; the
storage layout and sizing rules follow its "Saving History to Flash" guide).

```yaml
smartslot:
  history:
    size: 8MB          # `tsdb` partition; 24MB on a 32 MB module
    interval: 5min     # one averaged row per interval (1min..60min)
    update_interval: 10s   # how often the driver values are sampled into the average
time:
  - platform: sntp     # rows are keyed by real time; nothing is written until the clock is set
```

- Rows are the average of the samples in the interval (on-battery is the percentage of samples with `OB` in
  `ups.status`), so the chart draws a 5-minute rollup rather than raw polls. The page buckets further: 30 days come
  back hourly. Daily bars show minutes on battery per day.
- Sizing: 18 bytes per row and the ring file is kept to half the partition (LittleFS is copy-on-write and needs free
  blocks). 8 MB holds about 2 years at 5 min; 24 MB about 6 years. Halving the interval halves the span.
- Each row is one flash write (about 0.5 s on a writer task; the ESPHome loop is not blocked). Snapshots during an OTA
  are skipped. The stats line on the page shows rows, capacity, writes, evictions and partition use.

**Partition table and first install.** `history:` appends a `tsdb` partition to ESPHome's generated partition table
and the two app slots shrink to fit (16 MB: about 3.7 MB each, 8 MB tsdb). A partition table change is not applied by
OTA, so the first install of a build with `history:` (and any later change of `size:`) must be flashed over USB
serial (`esphome run` with the board on USB). Until then the card boots with the old table, logs
`no tsdb partition in flash` and runs without history. The `flash_size:` under `esp32:` must match the module:
`16MB` with `size: 8MB`, or `32MB` with `size: 24MB`. Changing `size:` later reformats the partition and drops the
stored history.

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
