# ESP32-P4 SmartSlot network card with a NUT-compatible server — design

Date: 2026-10-25. Status: approved by the user 2026-10-25. No further measurements will be taken; the four photo-derived numbers (H0, H1, F, F2) are used with clearance margins, see "Open until measured". Lives here until the project gets its own repo (my call).

## Intent

A plug-in card for the APC SmartSlot that reads the UPS over its native serial protocol and presents it on Ethernet as a
NUT `upsd` server, so Home Assistant, `upsmon` and `upsc` see a normal NUT UPS. It replaces the APC network management
card (AP9630/AP9631/AP9641) and the OpenNMC Linux card with a small ESP32-P4 board and open firmware.

Why: the user has a Smart-UPS SMT750 with an empty SmartSlot and an SMX3000RMLV2UNC that came with an AP9641. Both
speak APC Microlink, which is now open in NUT (`apcmicrolink`, GPL-2, 2026). OpenNMC's hardware files are not public
and its Nagami SoM is hard to buy; the ESP32-P4 does the whole job with one chip, no Linux.

## Goals

1. Works in the SMT750 and the SMX3000RMLV2UNC (both Microlink).
2. NUT clients (Home Assistant NUT integration, `upsc`, `upsmon`) connect on TCP 3493 and get status, variables and
   instant commands (shutdown, beeper, test) from the standard `apcmicrolink` driver.
3. Ethernet only, DHCP by default, static IP settable.
4. Powered from the SmartSlot 24 V rail. No external power.
5. Hot-swappable in the slot the way APC cards are (power fingers make first).
6. Programmable and debuggable over a front-panel USB-C port without opening the UPS.
7. Fits the existing card outline and bracket so it slides into the slot and screws down.

## Non-goals (not in this version)

Wi-Fi, web UI, SNMP, HTTPS/TLS, OTA update, USB host, RTC and coin cell, the OpenNMC expansion headers, the
SmartSlot DB9 passthrough UART (pins 7/12), CAN (15/16), power-on/off lines (13/14, SUA only), battery sense (5/10),
classic `apcsmart` (SUA/SMC) protocol. The CARD pins are routed to test pads only so a later board can use them.

## Anti-goals

- A card that can hang the UPS: the firmware must never hold the UPS serial line in an undefined state, and the
  hardware must not back-feed or short slot rails. If in doubt the card does nothing.
- Modifying the `apcmicrolink` driver source beyond the minimum (ideally zero). It must stay diff-able against NUT.
- A second CPU, Linux, or any board-to-board connector besides the Waveshare module.

## Constraints

- **Compute module (theirs):** Waveshare ESP32-P4-Module (ESP32-P4NRW32X, 32 MB PSRAM, 16 or 32 MB flash, castellated
  1 mm pitch, 88 pins, 25 × 25 mm, GPIO0–54 exposed, 3.3 V single supply). The ESP32-C6 on the module is left
  unpowered/unused.
- **Licence:** `apcmicrolink.c` is GPL-2, so the firmware is GPL-2 or later. ESP-IDF (Apache-2) is compatible.
- **UPS protocol:** Microlink, 9600 8N1, bytes NEXT 0xFE, INIT 0xFD, STOP 0xF7, descriptor read at runtime. Use
  driver option `cmdsrc=smartslot1`.
- **Serial mux:** SmartSlot pin 11 selects whether the UPS front DB9/RJ45 or the card talks to the UPS. NetCube's
  `S60uartmux` drives `UART_SEL_CTL` **low (0) for Microlink**, high (1) for classic smart protocol, high when the
  card is not configured to manage a UPS, and low on stop. Reading: on classic SUA models the card must assert
  pin 11 to steal the UPS's single UART from the DB9; Microlink models give the slot its own port (`smartslot1`),
  so pin 11 stays de-asserted (low) and the front DB9/RJ45 keeps working while the card runs. Holding it low for
  ever is therefore harmless (my inference from the script; the bench test checks the front port still answers
  with the card active). Default low, configurable from the console.
- **Microlink probe (from `S60uartmux`):** at 9600 baud send 0xFD, read for 1.5 s, expect at least 8 bytes not
  starting with 0x00.
- **Logic level on pins 8/9 is unknown** and will not be measured. ESP32-P4 pins are not 5 V tolerant, so the
  card is built level-agnostic: RX through a 74LVC1G17 (5 V-tolerant input, 3.3 V output), TX through an
  open-drain 74LVC1G07 with a 47 kΩ pull-up to 3.3 V, so the line idles at whatever level the UPS's own pull-up
  sets (3.3 V or 5 V) and the driver only ever pulls low. 9600 baud tolerates the slower open-drain edges.
- **Slot power:** OpenNMC's pinout and an independent Schneider-forum measurement on a SUA750I both put +12 V on
  pin 1 and +24 V on pin 3 with grounds on 2 and 4; Schneider's FAQ says the 24 V and ground fingers are the long,
  make-first pair, which implies the rail is live with no card fitted. The user's own probe read 0 V on every
  contact, so the card does not bet on one rail: pins 1 and 3 are diode-ORed (Schottky) into a buck rated for
  4.5–36 V input (LMR33620-class), and either rail runs it. Pin 6 (+5 V) goes to a test pad. APC rates its
  cards at roughly 2.5 W from the slot; this card needs ≈ 1.7 W, ≈ 150 mA at 12 V or 90 mA at 24 V.
- **NUT version:** vendor the `apcmicrolink*` and `apc_common*` files from NUT master as fetched 2026-10-25
  (`drivers/apcmicrolink.c` 4323 lines) and record the commit hash in `NUT_VERSION`. The shim list comes from
  grepping those exact files: dstate_setinfo/getinfo/addcmd/setflags/setaux/dataok/datastale, status_init/set/
  commit, alarm_init/set/commit, upsdebugx/upslogx/upsdebug_hex, fatalx, getval/testvar/addvar, nut_cmdname,
  ser_open/close/set_speed/set_dtr/send_buf/get_char/flush_io/comm_fail/comm_good, upsh.instcmd/setvar, plus
  `nut_debug_level` and the status/alarm map tables. About 35 symbols; re-grep after any NUT bump.
- **Toolchain:** ESP-IDF 5.4 or newer, C. Tests with the IDF Unity/`pytest-embedded` setup.

## Mechanical (what is known, 2026-10-25)

Reference card: the NMC3 pulled from the SMX3000RMLV2UNC; its label reads Model AP9640, board 640-2427A-Z REV01 (the AP9640/AP9641 share this PCB, so "AP9641" below means this board). Letters refer to
`smartslot-measurements.svg`.

| Item | Value | Source |
| --- | --- | --- |
| A bracket-side board edge → tab tip | 107.52 mm | measured |
| A2 bracket-side board edge → main right edge | 103.18 mm | measured |
| B board height | 101.81 mm | measured |
| C tab width | 27.58 mm | measured |
| D tab protrusion (A − A2) | 4.34 mm | derived |
| Finger pitch | 2.54 mm | user |
| E top board edge → tab top edge | 6.92 mm | measured (photo estimate was 8.5; treat other photo numbers as ±1.5 mm) |
| Tab region shape | the tab sits in a bay: the board is cut back ≈ 8.5 mm from the main right edge both above and below it. Above: the cut is open to the top edge (height = E = 6.92) and the hole sits in that corner. Below: a closed notch H2 = 5.06 mm tall (measured) × ≈ 8 mm deep (photo), then the edge returns to the main line. The bay and notch depths (H0 ≈ 8.5, H1 ≈ 8) come from the ruler photo and are cut at 9.0 on our board | H2 measured; depths from the ruler photo ±0.3 mm |
| F / F2 finger lengths | gold runs ≈ 9.2 mm on short fingers and ≈ 10 mm on the two long ones, so it starts ≈ 5 mm inboard of the main board edge; the long pair is only ≈ 1 mm longer | ruler photo (17 px/mm), ±0.3 mm |
| W finger width | 1.35 mm | measured |
| G tab top edge → pin 1 centre | 2.09 mm; so pin 19 centre is 27.58 − 2.09 − 9 × 2.54 = 2.63 mm from the tab bottom edge | measured |
| Holes | none needed: the AP9641's holes are unused, the card is held by the bracket screws and the slot guides | user |
| J PCB thickness | 1.52 mm (0.060 in) | measured; fabricate at the standard 1.6 mm, within card-edge connector tolerance |
| K bracket plate | 37 × 120.72 mm; the plate overhangs the 101.81 mm board by ≈ 9.5 mm at each end | measured |
| K screw fixings | U-shaped open slots in the plate edge, 5.84 mm deep, 16.18 mm from each end (so ≈ 88.4 mm apart if measured to slot centres); slot width not given, assume 4 mm for an M3/#6 screw | measured |
| K2 board offset | 1.42 mm: the board edge sits against the plate's inner face, so K2 is the plate thickness. Check: 1.42 + 103.18 = 104.6 mm to the tab tip against 98 mm to the slot contacts plus ≈ 5–6 mm of inboard gold: consistent | measured |
| M slot | chassis window 28 mm high × 95 mm wide; inside it a 5 mm high slit, wider than the window, takes the board (so ≥ 101.81 mm wide); depth from chassis face to the contacts 98 mm | measured. Note the 95 mm window: the bracket plate covers it and only the RJ45/USB-C openings need to fall inside it |
| L orientation | bracket left, components up: the tab is on the right edge near the top. The component face carries the odd pins, silkscreen "1 J2" at the top finger and "19" at the bottom; so pins 1 (+12 V) and 3 (+24 V) are the top two fingers on the component face, 2/4 (GND) behind them | close-up photo |

Pin numbering is settled by the AP9641 silkscreen: odd pins on the component face, pin 1 at the top of the tab.
Each signal finger passes through a ferrite bead (L5–L18) and a TVS array (D4, D5, D7, D13) on the APC card; the
main buck is a TPS54340 (42 V input) with a 10 µH inductor and an MBRS360 diode, which confirms the card is built
for the 24 V rail. Our card keeps the beads and TVS on the UART lines.

The board outline is a near-square ~103 × 102 mm. The design keeps the AP9641 outline and bracket so
the user can reuse or copy its bracket; the ESP32 board needs far less area, so the inside is mostly empty.

## Hardware

### Block diagram

```
SmartSlot edge (20 fingers)
  1 (+12V) ─┐
  3 (+24V) ─┴ Schottky OR ─ polyfuse 0.5A ─ TVS ─ buck 4.5–36V→3.3V (≥1.5A) ─ ESP_3V3 ─► module, PHY, LEDs
  2,4 (GND)
  8 (UPS TX → card RX) ─ 74LVC1G17 @3.3V (5V-tolerant input) ─► UART1 RX
  9 (card TX → UPS RX) ◄─ 74LVC1G07 open-drain, 47k pull-up to 3.3V ◄─ UART1 TX   (idles at the UPS's level)
 11 (UART_SEL) ◄─ GPIO, default low (Microlink)
  1,5,6,7,10,12..20 ─ test pads only
Module ─ RMII ─ IP101GRI PHY ─ MagJack RJ45 (bracket)
Module ─ USB D+/D- (native USB-serial-JTAG) ─ USB-C (bracket), ESD array
Buttons: RESET (EN), BOOT. LEDs: power, status.
```

### Parts (carrier only)

| Function | Part | Notes |
| --- | --- | --- |
| Compute | Waveshare ESP32-P4-Module | land pattern DXF in hand |
| Ethernet PHY | IC Plus IP101GA (LQFP-48, hand-solderable; IP101GRI QFN if PCBA), 25 MHz crystal, PHY outputs the 50 MHz REF_CLK into GPIO50, PHY addr 1 | same PHY family as Espressif's P4 EV board, so the IDF/ESPHome config carries over |
| RJ45 | MagJack with LEDs, e.g. HanRun HR911105A or Würth 7499111447 (what the AP9641 uses) | tab-up, bracket mounted |
| Buck | 4.75–36 V in → 3.3 V: Traco TSR 1-2433 module, 1 A, hand-solderable (LMR33620 if PCBA) | runs from 12 V or 24 V; see esp32-p4-smartslot-bom.md |
| Input | two Schottky diodes ORing pins 1 and 3, 0.5 A polyfuse, SMBJ33A TVS | SmartSlot rails are UPS-internal and quiet |
| UART RX | 74LVC1G17 (Schmitt, 5 V-tolerant input, 3.3 V supply) | works for 3.3 V or 5 V UPS signals |
| UART TX | 74LVC1G07 open-drain + 47 kΩ pull-up to 3.3 V, 100 Ω series | never drives the UPS line high; idles at the UPS's own level |
| Console | USB-C receptacle, 5.1 kΩ CC pulldowns, ESD array | VBUS unused (card powered from slot); D± to module GPIO24/25 |
| Buttons | 2 tactile (EN, BOOT) | reachable from the bracket or inside |
| LEDs | power (from 3.3 V), status (GPIO) | |

### ESP32-P4 pin map (from ESP-IDF ethernet example defaults for P4)

| Signal | GPIO |
| --- | --- |
| RMII TXD0 / TXD1 / TX_EN | 34 / 35 / 49 |
| RMII RXD0 / RXD1 / CRS_DV | 29 / 30 / 28 |
| RMII REF_CLK (50 MHz, PHY-generated with loopback input on GPIO32, or P4-generated on 50) | 50 (+32) |
| MDC / MDIO | 31 / 52 |
| PHY reset | 51 |
| UART1 TX / RX to SmartSlot | 2 free GPIOs, builder's choice (any; GPIO matrix) |
| UART_SEL (pin 11) | 1 free GPIO |
| Status LED | 1 free GPIO |
| USB D- / D+ | 24 / 25 (fixed, USB-serial-JTAG) |
| BOOT | strapping pin per P4 datasheet (GPIO35 shares RMII TXD1 on Espressif's own board; copy their pull/button arrangement) |

GPIO34–38 are strapping pins; copy the EV board's resistor values. Builder verifies all of the above against the
ESP32-P4 TRM and the Waveshare pin table before layout.

### Power budget

ESP32-P4 at 360 MHz with Ethernet ≈ 250–380 mA at 3.3 V, PHY ≈ 60 mA, LEDs ≈ 20 mA: ~0.5 A at 3.3 V ≈ 1.7 W,
≈ 90 mA from 24 V at 80 % efficiency. The AP9641 draws similar power from the same rail, so the slot can supply it.

### Hot swap

Pins 1–4 (12 V, 24 V and both grounds) are the long fingers and connect first. Inrush is limited by the buck's soft start; keep bulk
input capacitance ≤ 22 µF. UART and UART_SEL pins have series resistance so a partially inserted card cannot drive
the UPS lines hard.

## Firmware

ESP-IDF project, FreeRTOS, three tasks plus the IDF network stack.

### Components

1. **`nutshim`** — the ~35 NUT framework functions the driver calls, implemented small:
   - `dstate_*`: a mutex-protected table of `(name, value, flags, aux)` plus a command list; `dstate_dataok/
     datastale` set a flag `upsd` reads.
   - `status_*`, `alarm_*`: string builders as in NUT's `dstate.c`/`status`.
   - `upsdebugx/upslogx/upsdebug_hex` → `ESP_LOG*`; `fatalx` → log and reboot after 5 s.
   - `getval/testvar/addvar`: fixed table from NVS (`cmdsrc`, `baudrate`).
   - `ser_*`: thin wrappers over `uart_driver_install`, `uart_read_bytes`, `uart_write_bytes`, `uart_flush`;
     `ser_set_dtr` is a no-op; `ser_comm_fail/good` count and log.
   - `upsh.instcmd/setvar` hooks and `nut_cmdname`/`nut_stdint` headers copied or stubbed.
2. **`apcmicrolink`** — upstream `apcmicrolink.c/.h`, `apcmicrolink-maps.c/.h`, `apc_common.c/.h` vendored
   verbatim with a `NUT_VERSION` file. Build with the IDF toolchain; any needed change goes in as a patch file.
   `upsdrv_initups → initinfo → updateinfo` every 2 s in a `driver` task.
3. **`upsd`** — TCP 3493 server, up to 4 clients, line protocol:
   `VER`, `NETVER`, `LIST UPS`, `LIST VAR <ups>`, `LIST RW <ups>`, `LIST CMD <ups>`, `LIST ENUM/RANGE <ups> <var>`
   (empty lists), `GET VAR/TYPE/DESC/CMDDESC/UPSDESC/NUMLOGINS`, `USERNAME`, `PASSWORD`, `LOGIN`, `LOGOUT`,
   `INSTCMD` (queued to the driver task), `SET VAR`, `STARTTLS` → `ERR FEATURE-NOT-CONFIGURED`,
   `ERR UNKNOWN-COMMAND` otherwise. One UPS named `ups` with description from `device.model`. Data stale →
   `ERR DATA-STALE`. Credentials from NVS. Reads never need a login. INSTCMD and SET need USERNAME/PASSWORD
   matching a user set from the console; with no user configured they answer `ERR ACCESS-DENIED`. No default
   password ships (my call: an unconfigured card can report but cannot shut the UPS down).
4. **`net`** — IDF `esp_eth` with IP101, DHCP, hostname `smartslot-<mac>`, static IP from NVS when set, mDNS
   `_nut._tcp` advertisement (optional, cheap).
5. **`console`** — IDF `esp_console` on USB-serial-JTAG: `net`, `nutuser`, `cmdsrc`, `uartsel`, `probe`, `reboot`,
   `upsc` (dumps the dstate table). This is the only configuration path.

### Behaviour

- Boot: bring up Ethernet, set UART_SEL low, run the Microlink probe; if it answers start the driver, else retry
  every 30 s and keep `upsd` up with `ERR DATA-STALE`.
- Status LED: off = no link, blink = no UPS data, on = data OK.
- Watchdog on the driver task; a hung driver reboots the card, not the UPS.

### Testing

- `nutshim` and `upsd` unit tests on host (`idf.py --preview set-target linux`) with a fake dstate table.
- Protocol test: NUT's own `upsc`, `upscmd`, `upsrw` and the Home Assistant NUT integration against the card.
- **Bench before PCB:** an Espressif ESP32-P4-Function-EV-Board (same PHY and pins) plus a MAX3232 and APC
  940-0625A cable to the UPS front RJ45 port, with `cmdsrc=rj45`. This validates the whole firmware against the
  real SMT750 before any board is made.

## What is left to the builder

- Exact free-GPIO choices for UART1, UART_SEL, LED; buck part and layout; TVS values; ESD parts; connector part
  numbers; silkscreen.
- Bracket drawing from K/K2/M; no mounting holes; until then the outline is A2 × B with the tab at
  C/D/E as above (E measured).
- KiCad project structure, Gerber output, JLCPCB/PCBWay fabrication; the user has no KiCad in this container, so
  the builder owns the CAD tool.
- Whether to add a 4-pin header for the CARD pins instead of test pads (no cost either way).

## Calls I made (check these)

- Keep the AP9641 outline and bracket rather than a smaller board: it guarantees slot fit at the cost of empty FR4.
- GPL-2 firmware, driver vendored unmodified.
- IP101GRI PHY and the EV-board pin map, for a known-good IDF path.
- Level-agnostic UART (74LVC1G17 in, open-drain 74LVC1G07 out) and diode-ORed 12 V/24 V input, so no slot voltage
  needs measuring before the board is made.
- No OTA in v1; USB-C flashing from the front panel instead.
- Console-only configuration; no web UI.
- One NUT UPS named `ups`, 4 clients, plain-text auth as NUT does without TLS; no default user, commands locked
  until one is set from the console.
- Layout does not wait for any electrical measurement; the user chose not to probe the slot.
- Pin 11 held low permanently (Microlink needs no mux), verified on the bench.
- NUT source pinned to the 2026-10-25 master snapshot already downloaded.

## Open until measured

| Item | Blocks |
| --- | --- |
| H0, H1 (bay and notch depth), F, F2 | photo-derived ±0.3 mm from the ruler photo (17 px/mm). The user will not measure further, so the board uses them with margins that only add clearance: bay and notch cut 9.0 mm deep (photo 8.5 / 8.0), finger gold 9.7 mm short and 10.5 mm long (photo 9.2 / 10.0), keeping the 0.8 mm make-first lead. Deeper cuts and longer gold cannot stop the card seating; nothing is left to measure |
