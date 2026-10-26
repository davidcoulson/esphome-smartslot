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
need the configured user. There is no TLS, Wi-Fi or mDNS in the component; SNMP is the separate `snmp_agent` component below.

## SNMP agent (`snmp_agent`)

Read-only SNMPv1/v2c agent on UDP 161, so existing APC SNMP integrations (NUT `snmp-ups`, Home Assistant SNMP,
LibreNMS, Zabbix) keep working. Add `snmp_agent` to `components:` in `external_components`; the full default OID
table is in `example/smartslot-bench.yaml`. Options: `port` (161), `community` (`public`, read-only; any other
community is dropped silently), `sys_descr`, `sys_object_id` (default `1.3.6.1.4.1.318.1.3.2`, which is what
NUT's `apcc` MIB matches on), `sys_name` (default: node name), `sys_contact`, `sys_location`, and `readings:`.

Each reading maps one OID to a `sensor:` or `text_sensor:` id (or a fixed `value:`), with `type:` `integer`, `gauge`,
`timeticks`, `string` or `oid`, and an optional `scale:` multiplier. A text sensor can become a number with
`map:` (first space-separated token of the state that matches wins, `"*"` is the fallback), e.g. `ups.status` to
`upsBasicOutputStatus`. An OID with no value yet (sensor not published) reads as noSuchInstance and is skipped by walks.

Served by default (the example table):

| Group | OIDs |
|---|---|
| System, 1.3.6.1.2.1.1 | sysDescr, sysObjectID, sysUpTime, sysContact, sysName, sysLocation (built in) |
| APC PowerNet, 1.3.6.1.4.1.318.1.1.1 | upsBasicIdentModel `.1.1.1.0`, upsBasicBatteryStatus `.2.1.1.0`, upsAdvBatteryCapacity `.2.2.1.0`, upsAdvBatteryTemperature `.2.2.2.0`, upsAdvBatteryRunTimeRemaining `.2.2.3.0`, upsAdvInputLineVoltage `.3.2.1.0`, upsAdvOutputVoltage `.4.2.1.0`, upsAdvOutputLoad `.4.2.3.0`, upsBasicOutputStatus `.4.1.1.0` |
| UPS-MIB (RFC 1628), 1.3.6.1.2.1.33.1 | upsIdentManufacturer/Model `.1.1/.1.2`, upsBatteryStatus `.2.1`, upsEstimatedMinutesRemaining `.2.3`, upsEstimatedChargeRemaining `.2.4`, upsBatteryTemperature `.2.7`, upsInputNumLines `.3.2`, upsInputVoltage `.3.3.1.3.1`, upsOutputSource `.4.1`, upsOutputNumLines `.4.3`, upsOutputVoltage `.4.4.1.2.1`, upsOutputPercentLoad `.4.4.1.5.1` |

Not supported: SNMPv3 (USM auth/priv; planned), traps/informs, SET (answered with readOnly/noAccess), and PowerChute
Network Shutdown (proprietary protocol, not SNMP). Only the OIDs above exist: tools that need other PowerNet
objects (outlet groups, self-test, runtime calibration) will see noSuchObject until you add readings for them. Note
the card's `sys_name` etc. are fixed at build time. NUT's `apcc` MIB reads more than the table above (serial number,
firmware, battery voltage); missing OIDs are skipped, so `snmp-ups` still works with fewer variables.

### Testing

Host tests (BER codec, OID ordering, GET/GETNEXT/GETBULK, v1 errors, wrong community, SET refusal, corrupted
packets under ASan/UBSan): `make -C tests snmp`.

Try a client without hardware (demo table on 127.0.0.1:1161):

```bash
make -C tests snmp-serve   # then, in another shell:
snmpwalk -v2c -c public 127.0.0.1:1161 1.3.6.1.4.1.318
```

Manual test against the card (replace `<ip>`; the walk must end, not hang):

```bash
snmpwalk -v2c -c public <ip> 1.3.6.1.4.1.318
snmpwalk -v2c -c public <ip> 1.3.6.1.2.1.33
snmpwalk -v1  -c public <ip> 1.3.6.1.2.1.1
snmpwalk -v2c -c wrong -t2 -r0 <ip> 1.3   # must time out: wrong community is dropped
```

NUT `snmp-ups` (`/etc/nut/ups.conf`), then `upsc card@localhost`:

```ini
[card]
    driver = snmp-ups
    port = <ip>
    mibs = apcc
    community = public
    snmp_version = v2c
```

Not yet run against hardware or a real `snmp-ups`; the OID table follows the published PowerNet and RFC 1628 MIBs.

## Layout

- `components/smartslot/` ESPHome glue (`*.py`, `smartslot*.cpp/h`) and `CMakeLists.txt`, which ESP-IDF builds
  together with `nut/` (vendored driver, byte-identical, see `nut/SHA256SUMS`), `shim/` (NUT API on ESP-IDF) and
  `upsd/` (protocol and server).
- `components/snmp_agent/` SNMP agent: `snmp_core.*` (BER codec and request handler, no ESPHome dependency) and the ESPHome glue.
- `tests/` host tests: `make -C tests` (gcc + Unity) and `make -C tests snmp` (SNMP core).
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
