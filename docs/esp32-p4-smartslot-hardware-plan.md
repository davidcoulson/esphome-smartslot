# ESP32-P4 SmartSlot card hardware implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A KiCad 8 project (schematic, layout, Gerbers, BOM, bracket drawing) for a SmartSlot card carrying the Waveshare ESP32-P4-Module, IP101GA Ethernet (IP101GRI if PCBA), a 12/24 V buck (TSR 1-2433 module, see esp32-p4-smartslot-bom.md) and the level-agnostic UART interface, matching the AP9640/AP9641 outline.

**Architecture:** Hierarchical schematic with one sheet per block (edge connector, power, module, Ethernet, UART interface, USB console). A 2-layer, 1.6 mm board on the measured outline; all parts on the component (odd-pin) face; the tab fingers are a custom footprint generated from the measurements so the plan's numbers, not a library guess, define the connector.

**Tech Stack:** KiCad 8 (`kicad-cli` for ERC/DRC/Gerbers so the steps are scriptable), the Waveshare module land-pattern DXF and 3D model already in the scratchpad (`p4mod/land/`, `p4mod/3d/`), JLCPCB or PCBWay 2-layer service.

**Spec:** `docs/superpowers/specs/2026-10-25-esp32-p4-smartslot-design.md`. Firmware is `docs/superpowers/plans/2026-10-25-firmware.md`; the pin numbers below must match its Global Constraints.

## Global Constraints

- No further measurements will be taken. The photo-derived values (H0 ≈ 8.5, H1 ≈ 8.0, F ≈ 10.0, F2 ≈ 9.2) are replaced by clearance-safe values: cutouts 9.0 deep, gold 10.5 (long) / 9.7 (short). Deeper cuts and longer gold only add clearance.
- Board: 1.6 mm FR4, 2 layers, 1 oz, ENIG with hard-gold (or at least ENIG) on the fingers, 20° × 0.5 mm chamfer on the tab tip; coordinate origin = bracket-side board edge (x = 0) and top board edge (y = 0), x to the right, y downward, component side up, bracket left (spec item L).
- Outline (mm, from the spec table): top edge (0,0)→(94.18,0); bay wall down to (94.18, 6.92); tab top edge to (107.52, 6.92); tab tip down to (107.52, 34.50); tab bottom edge back to (94.18, 34.50); notch wall down to (94.18, 39.56); notch bottom out to (103.18, 39.56); main right edge down to (103.18, 101.81); bottom edge to (0, 101.81); bracket edge up to (0,0).
- Fingers: 10 per face, pitch 2.54, width 1.35, pin 1 centre at y = 6.92 + 2.09 = 9.01, pin 19 at y = 31.87; odd pins on the component face (F.Cu), even on B.Cu directly behind; long fingers (1, 3 front; 2, 4 back) gold from x = 97.02 to the tip (10.5 long), short ones from x = 97.82 (9.7 long).
- Bracket: plate 37 × 120.72 × 1.42 mm, U-slots 5.84 deep, 16.18 from each end, 4 mm wide; the board edge sits against the plate's inner face (K2 = 1.42); plate overhangs the board by (120.72 − 101.81)/2 = 9.455 mm at each end; RJ45 and USB-C openings must fall inside the 95 mm chassis window, i.e. between y = 3.4 and y = 98.4 on the board.
- Nets to the slot: 1 (+12 V), 3 (+24 V) → diode-OR; 2, 4 → GND; 8 → UART RX path; 9 → UART TX path; 11 → UART_SEL; 5, 6, 7, 10, 12–20 → 1.27 mm test pads only; 6 (+5 V) labelled `SLOT_5V_TP`.
- Input chain: SS34-class Schottky ×2 → 0.5 A polyfuse (e.g. 1812L050) → SMBJ33A → 22 µF (≤ 22 µF total) → Traco TSR 1-2433 (SIP-3; LMR33620 if PCBA) → 3.3 V 1 A → 22 µF ×2.
- UART: pin 8 → 100 Ω → 74LVC1G17 (VCC 3.3 V) → `CONFIG_SMARTSLOT_UART_RX_GPIO` (48); `CONFIG_SMARTSLOT_UART_TX_GPIO` (47) → 74LVC1G07 → 100 Ω → pin 9, with 47 kΩ from the 74LVC1G07 output to 3.3 V; ferrite bead (600 Ω @ 100 MHz, 0603) and a 24 V-rated TVS array on pins 8, 9, 11 at the finger end.
- UART_SEL: GPIO 53 → 1 kΩ → pin 11, 100 kΩ pull-down at the pin (so it is low while the P4 is in reset).
- Ethernet: IP101GA (LQFP-48; IP101GRI if PCBA), 25 MHz crystal, PHY addr 1 (strap), RMII pins exactly as the firmware plan's Global Constraints, the PHY generates the 50 MHz REF_CLK and drives it into P4 GPIO50 (clock input, as on the EV board and the user's gps-ntp-p4 ESPHome config); MagJack with integrated magnetics and LEDs (HR911105A or Würth 7499111447).
- USB console: USB-C receptacle, CC1/CC2 5.1 kΩ to GND, D+/D− to GPIO25/24, ESD array (e.g. USBLC6-2SC6), VBUS not connected to the board rails.
- Strapping GPIO34–38: copy the ESP32-P4-Function-EV-Board pull resistor values; BOOT button on the EV board's boot pin, EN button on module EN.
- Status LED on GPIO 54 via 1 kΩ; power LED from 3.3 V via 1 kΩ.

## Review Focus

1. A card inserted into a classic SUA slot (pins 13/14 power-on/off, different mux sense) must do nothing harmful: all unused fingers are test pads with no components, and UART_SEL is pulled down. Check in Task 3 ERC netlist: pins 5, 6, 7, 10, 12–20 have exactly one connection (the pad).
2. The tab must slide into a 5 mm slit at 98 mm depth and reach the contacts: the outline check in Task 5 compares the Edge.Cuts polygon against the Global Constraints numbers to 0.05 mm.
3. 24 V on both pins 1 and 3 simultaneously (both rails live) must not back-feed: two diodes, no direct junction before them; Task 3 ERC confirms no net joins pins 1 and 3.
4. A 5 V UPS UART must not reach the P4: the RX path has the 74LVC1G17 and nothing bypasses it; Task 3 netlist check `pin 8 → R → U_rx → GPIO48` is the only path.
5. The PHY must come out of reset after the P4 (RMII strap timing): PHY_RST on GPIO51 with a 10 kΩ pull-down, as on the EV board; Task 2 copies it.

---

### Task 1: Outline file

**Files:**
- Create: `hardware/mech/outline.csv` (the polygon above, one vertex per line), `hardware/mech/measurements.md` (the spec's table plus the photo values and the clearance values used)

- [ ] **Step 1: Write `measurements.md`**: the spec's table verbatim plus the four photo values and the clearance values chosen above, so the numbers in `outline.csv` are traceable.
- [ ] **Step 2: Write `outline.csv`**; verify the polygon closes and its bounding box is 107.52 × 101.81 with a 10-line Python check (`python3 -I hardware/mech/check_outline.py` prints `closed, bbox 107.52 x 101.81`).
- [ ] **Step 3: Commit** — `git add hardware/mech && git commit -m "Mechanical: outline from the measurements"`.

### Task 2: Symbols, footprints and schematic

**Files:**
- Create: `hardware/smartslot-p4.kicad_pro`, `hardware/smartslot-p4.kicad_sch` plus sheets `edge.kicad_sch`, `power.kicad_sch`, `module.kicad_sch`, `eth.kicad_sch`, `uart.kicad_sch`, `usb.kicad_sch`
- Create: `hardware/lib/smartslot.pretty/ESP32-P4-Module.kicad_mod` (from the DXF: 88 castellated pads, 1 mm pitch, 25 × 25 mm), `hardware/lib/smartslot.pretty/SmartSlot_Edge_20.kicad_mod` (generated by `hardware/lib/gen_edge.py` from the finger numbers), `hardware/lib/smartslot.kicad_sym` (module symbol with Waveshare's pin names, edge connector symbol with the 20 spec pin names)

- [ ] **Step 1: Generate the edge footprint** — `python3 hardware/lib/gen_edge.py > hardware/lib/smartslot.pretty/SmartSlot_Edge_20.kicad_mod`. Footprint origin: x = 0 at the tab tip, y = 0 at the tab top edge, x negative inboard. The script places 10 F.Cu pads (odd pins) and 10 B.Cu pads (even pins, same positions): pin 2k−1 / 2k centre y = 2.09 + (k−1) × 2.54, width 1.35, no solder mask, `smd` type; long pins 1–4 span x = −10.5…0 (centre −5.25), short pins span x = −9.7…0 (centre −4.85). Expected: opening the footprint in KiCad shows 20 pads, pad 1 at (−5.25, 2.09), pad 5 at (−4.85, 7.17), pad 19 at (−4.85, 24.95).
- [ ] **Step 2: Draw the six sheets** per Global Constraints; every slot pin not in the nets list ends on a `TestPoint:TestPoint_Pad_D1.5mm` footprint.
- [ ] **Step 3: ERC** — Run: `kicad-cli sch erc hardware/smartslot-p4.kicad_sch -o build/erc.rpt --severity-error --exit-code-violations` Expected: exit 0.
- [ ] **Step 4: Netlist checks for Review Focus 1, 3, 4** — `kicad-cli sch export netlist --format kicadxml -o build/net.xml` then `python3 -I hardware/lib/check_nets.py build/net.xml` prints `pins 5,6,7,10,12-20: pad only — OK`, `pins 1 and 3 on different nets — OK`, `pin 8 reaches GPIO48 only through U_rx — OK`.
- [ ] **Step 5: Commit** — `git add hardware && git commit -m "Schematic: six sheets, module and edge footprints, net checks"`.

### Task 3: Layout

**Files:**
- Create: `hardware/smartslot-p4.kicad_pcb`

- [ ] **Step 1: Import `outline.csv` as Edge.Cuts** (KiCad's DXF import after converting the CSV with the same script, or drawn by coordinates), place the edge footprint so its pads land on the Global Constraints coordinates, add the tab chamfer.
- [ ] **Step 2: Place**: MagJack and USB-C against the bracket edge (x = 0) with their faces flush to the plate at x = −1.42 (connector bodies protrude through plate cutouts), both inside y = 3.4…98.4; module centred ≈ (50, 50) with the RMII group facing the PHY; PHY within 25 mm of the module and 50 mm of the MagJack; buck near the fingers at the tab; UART interface parts within 15 mm of pins 8/9/11; everything else inside the AP9640 footprint envelope.
- [ ] **Step 3: Route** with a ground pour both sides; RMII traces ≤ 5 cm, 50 Ω single-ended by the fab's 2-layer stack-up; USB D± 90 Ω differential; keep the switching node of the buck under 1 cm²; no copper within 1 mm of the tab edges except fingers.
- [ ] **Step 4: DRC** — Run: `kicad-cli pcb drc hardware/smartslot-p4.kicad_pcb -o build/drc.rpt --severity-error --exit-code-violations` Expected: exit 0 with the JLCPCB 2-layer rules loaded (`hardware/jlcpcb.kicad_dru`: 0.127 mm trace/space, 0.3 mm drill, 0.2 mm edge clearance).
- [ ] **Step 5: Outline check (Review Focus 2)** — `python3 -I hardware/mech/check_pcb_outline.py hardware/smartslot-p4.kicad_pcb hardware/mech/outline.csv` prints `max deviation 0.0x mm — OK` (≤ 0.05).
- [ ] **Step 6: Commit** — `git commit -am "Layout: 2-layer board on the measured outline, DRC clean"`.

### Task 4: Fabrication outputs, BOM, bracket drawing

**Files:**
- Create: `hardware/out/gerbers.zip`, `hardware/out/bom.csv`, `hardware/out/cpl.csv`, `hardware/mech/bracket.svg` (or `.dxf`)

- [ ] **Step 1: Gerbers + drill** — `kicad-cli pcb export gerbers --layers F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts -o build/gerbers/ hardware/smartslot-p4.kicad_pcb && kicad-cli pcb export drill -o build/gerbers/ hardware/smartslot-p4.kicad_pcb && zip -j hardware/out/gerbers.zip build/gerbers/*`. Expected: 9 files in the zip; the Edge.Cuts gerber viewed in `gerbv` (or JLCPCB's online viewer) shows the tab and both cutouts.
- [ ] **Step 2: BOM and placement** — `kicad-cli sch export bom --fields Reference,Value,Footprint,LCSC -o hardware/out/bom.csv …` and `kicad-cli pcb export pos --format csv --units mm -o hardware/out/cpl.csv …`; every row has an LCSC or Mouser part number (Waveshare module and MagJack are hand-soldered: mark `manual`).
- [ ] **Step 3: Bracket drawing**: plate 37 × 120.72 × 1.42 with the two U-slots, RJ45 and USB-C cutouts positioned from the layout (`kicad-cli pcb export svg` of the F.Fab layer gives the connector positions), board edge line at x = 1.42 from the plate's inner face. Note in the drawing that the user may instead reuse the AP9640 bracket if the connectors are placed at its openings (RJ45 at y ≈ 70–88 and the micro-USB opening at y ≈ 50–56 in the spec's overview drawing; measure before relying on it).
- [ ] **Step 4: Order one set of 5 boards and a stencil**; record the order number and the fab's DFM report in `hardware/out/order.md`.
- [ ] **Step 5: Commit and tag** — `git add hardware/out hardware/mech && git commit -m "Fab outputs: Gerbers, BOM, placement, bracket drawing" && git tag hw-v0.1`.

### Task 5: Bring-up (after boards arrive)

- [ ] **Step 1**: Populate power chain only; feed 12 V then 24 V on pins 1 and 3 via the fingers; expect 3.30 ± 0.05 V, < 10 mA idle; then the rest.
- [ ] **Step 2**: Flash the Task 9 firmware from the firmware plan over USB-C; `smartslot> net` shows a MAC and link up on the RJ45; `upsc -l <ip>` lists `ups`.
- [ ] **Step 3**: Insert into the SMT750 with `cmdsrc smartslot1` (the default): `upsc ups@<ip>` shows live data; the UPS front RJ45 still answers a 940-0625A cable (UART_SEL low leaves the front port alive); repeat in the SMX3000.
- [ ] **Step 4**: Record results in `docs/bench.md` and commit: `git commit -am "Bring-up: card runs in SMT750 and SMX3000"`.

## Self-review notes

- Spec coverage: mechanical table → Global Constraints outline and Task 1; parts table → Global Constraints and Task 2; pin map → shared with the firmware plan; power budget and hot swap → input chain (≤ 22 µF) and series resistors; "left to the builder" items (GPIO choices, buck, TVS, connectors) are now fixed values here so the implementer does not choose.
- Open: nothing; H0, H1, F, F2 are photo values with clearance margins, by the user's decision not to measure further.

## Rev A debug provisions (user request 2026-10-25: test points and ways to fudge anything that does not work)

Applies to Tasks 2 and 3; `check_nets.py` asserts every item below exists. All jumpers are SMD solder jumpers
(2-pad or 3-pad, 0.6 mm gap) unless a header is named; 0 Ω links are 0603 footprints. Defaults are what the
design doc specifies, so a board built as drawn behaves like the design; the fudges are DNP or open.

Test points (1.5 mm round pads, silkscreen-labelled, all on the component side):
- TP1–TP20: every SmartSlot edge pin, used or not, in pin order in one row near the tab.
- TP21–TP30: VIN after the diode-OR, VIN after the fuse, 3V3, GND ×2, P4-side UART TX, P4-side UART RX, UART_SEL at the GPIO, RMII REF_CLK, PHY nRESET.
- TP31–TP36: MDC, MDIO, EN, BOOT, USB D+, USB D−.

Fudges:
- UART level/logic: JP1, JP2 (0 Ω links) bypass U3 and U4, connecting the P4 pins straight to the 100 Ω series resistors, for a UPS that is happy at 3.3 V or if the gates misbehave. JP3 (3-pad) selects the pin-9 open-drain pull-up to 3V3 (default) or to pin 6 (+5 V). JP4 is a 2×2 crossover jumper pattern on pins 8/9 so TX and RX can be swapped with two solder blobs if the pin table was read backwards.
- UART_SEL: JP5 (3-pad) selects GPIO-driven (default), tied to GND, or tied to 3V3; cut the default trace to float it. R4 stays in series for probing.
- Power: JP6 (0 Ω, fitted) in the 3V3 rail for current measurement or to isolate the buck; J3 2-pin 2.54 mm header on 3V3/GND to inject bench 3.3 V with JP6 open. JP7 (solder jumper, OPEN by default) feeds USB-C VBUS into the buck input so the card runs from USB on the bench; silkscreen: "USB PWR — bench only, never in a UPS". JP8/JP9 allow dropping D1 or D2 to run from 24 V only or 12 V only. A second DNP buck footprint is not added: the TSR module is socketable (fit a 3-pin header) and any pin-compatible 3.3 V SIP module (RECOM R-78E3.3-0.5, OKI-78SR-3.3) drops in.
- Ethernet: Y2 DNP footprint for a 3225 50 MHz oscillator plus JP10 (3-pad) selecting REF_CLK from the PHY (default) or from Y2 into both the PHY clock input and GPIO50, covering the clock-direction uncertainty. PHY address and mode straps as paired pull-up/pull-down 0603 footprints (fit one of each pair). JP11 (3-pad) ties PHY nRESET to GPIO51 (default) or to the RC power-on reset. RJ45 LED series resistors as 0603 footprints so polarity can be fixed by swapping the two resistor positions.
- GPIO reassignment: J4 2×4 2.54 mm header carrying GPIO47, 48, 53, 54 (as routed) and four spare module GPIOs plus 3V3 and GND; the four routed signals reach the UART gates, UART_SEL and the LED through 0 Ω links JP12–JP15, so a busy GPIO can be re-routed with a wire from the header after lifting the link.
- Programming: J5 1×3 header on EN, BOOT, GND beside the buttons; J6 1×4 header on the module's UART0 TX/RX pins and GND/3V3 in case USB-serial-JTAG is unusable.

BOM impact (added to `esp32-p4-smartslot-bom.md`): ~36 test pads (free), ~12 solder jumpers (free), 8 × 0 Ω 0603, 2.54 mm headers (2-, 3-, 4-pin and 2×4), one DNP oscillator footprint, one DNP 3-pin SIP socket. Nothing else changes.
