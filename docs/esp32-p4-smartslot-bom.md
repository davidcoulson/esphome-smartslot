# ESP32-P4 SmartSlot card: parts list and who assembles it

Date: 2026-10-25. Goes with `smartslot-board-layout.svg` (placement) and `esp32-p4-smartslot-hardware-plan.md`
(the KiCad tasks). Prices are rough single-unit figures from memory of LCSC/Mouser; check at order time.

## Part choices, and why these over the design doc's first picks

The design doc named an LMR33620 buck and the IP101GRI (QFN-32) PHY. Both are PCBA-only parts for most
people, so the layout uses hand-solderable equivalents with the same electrical role. If the board goes to
a PCBA service anyway, either set works; the footprints differ, so decide before Task 2 of the hardware plan.

| Role | Chosen | Package | Hand-solder | Alternative (PCBA only) |
| --- | --- | --- | --- | --- |
| 12/24 V → 3.3 V buck | Traco TSR 1-2433 (4.75–36 V in, 1 A) | SIP-3 through-hole | easy | LMR33620 (HSOIC-8), ~1.5 A |
| Ethernet PHY | IC Plus IP101GA | LQFP-48, 0.5 mm pitch | moderate (drag solder + flux) | IP101GRI (QFN-32) |

1 A at 3.3 V covers the P4 module (≈ 0.5 A peak with Ethernet) plus the PHY (≈ 0.1 A) with margin. The
TSR module is fixed-output and needs no inductor, feedback network or compensation parts.

## Bill of materials (one card)

| Ref | Qty | Part | Package | Notes | ≈ USD |
| --- | --- | --- | --- | --- | --- |
| U1 | 1 | Waveshare ESP32-P4-Module (ESP32-P4NRW32, 16 MB flash, 32 MB PSRAM) | castellated module, 1 mm pitch | Waveshare/AliExpress; not in LCSC. Check silicon revision: pre-v3 needs `engineering_sample: true` in ESPHome, v3+ does not | 12 |
| U2 | 1 | IP101GA Ethernet PHY | LQFP-48 | LCSC/AliExpress; strap PHY address 1 like the Espressif EV board | 1.5 |
| Y1 | 1 | 25 MHz crystal, 18 pF, ±20 ppm | 3225 SMD | PHY makes the 50 MHz RMII REF_CLK from it and drives P4 GPIO50 (`CLK_EXT_IN`) | 0.3 |
| J1 | 1 | MagJack RJ45 with magnetics and 2 LEDs, tab up | through-hole | HanRun HR911105A or Würth 7499111447 (what the AP9640 uses) | 2–4 |
| J2 | 1 | USB-C receptacle, 16-pin, mid-mount or top-mount | SMD 0.5 mm pitch + TH posts | console/flash via USB-serial-JTAG (GPIO24/25) | 0.5 |
| R10, R11 | 2 | 5.1 kΩ | 0603 | USB-C CC pull-downs | 0.02 |
| D7 | 1 | USBLC6-2SC6 | SOT-23-6 | USB ESD | 0.3 |
| PS1 | 1 | Traco TSR 1-2433 | SIP-3 | buck, see above | 6 |
| D1, D2 | 2 | SS34 Schottky, 3 A 40 V | SMA (DO-214AC) | diode-OR of pin 1 (+12 V) and pin 3 (+24 V) | 0.2 |
| F1 | 1 | Polyfuse 0.5 A hold, 60 V | 1812 | e.g. Littelfuse 1812L050 | 0.4 |
| D3 | 1 | SMBJ33A TVS | SMB (DO-214AA) | input clamp after the fuse | 0.3 |
| C1 | 1 | 22 µF 50 V electrolytic or 2× 10 µF 50 V X7R | 6.3 mm radial or 1210 | input bulk; keep total ≤ 22 µF so the UPS slot rail is not shocked at insertion | 0.3 |
| C2, C3 | 2 | 22 µF 10 V X5R | 0805 | 3.3 V bulk | 0.2 |
| C4–C12 | ~9 | 100 nF 16 V X7R | 0603 | decoupling: module, PHY (4), gates, crystal load caps are separate | 0.1 |
| C13, C14 | 2 | 18 pF C0G | 0603 | crystal load | 0.05 |
| U3 | 1 | 74LVC1G17 Schmitt buffer | SC70-5 | UPS TX (pin 8) → P4 RX, tolerates 5 V input | 0.2 |
| U4 | 1 | 74LVC1G07 open-drain buffer | SC70-5 | P4 TX → UPS RX (pin 9) | 0.2 |
| R1 | 1 | 47 kΩ | 0603 | open-drain pull-up to 3.3 V on pin 9 | 0.02 |
| R2, R3 | 2 | 100 Ω | 0603 | series on pins 8 and 9 | 0.02 |
| R4 | 1 | 1 kΩ | 0603 | UART_SEL (pin 11) drive from GPIO53 | 0.02 |
| R5 | 1 | 100 kΩ | 0603 | UART_SEL pull-down (card idle = Microlink selected) | 0.02 |
| FB1–FB3 | 3 | Ferrite bead 600 Ω @ 100 MHz | 0603 | on pins 8, 9, 11 at the edge | 0.1 |
| D4–D6 | 3 | PESD5V0S1BA or SMF5.0A TVS | SOD-323 / SOD-123F | ESD on pins 8, 9, 11 | 0.2 |
| R6–R9 | 4 | 49.9 Ω 1 % | 0603 | RMII/MDI terminations per IP101GA datasheet (count may change at schematic time) | 0.05 |
| R12, R13 | 2 | 330 Ω | 0603 | RJ45 LED series | 0.02 |
| D8 | 1 | Green LED | 0603 | status LED on GPIO54 | 0.1 |
| R14 | 1 | 1 kΩ | 0603 | status LED | 0.02 |
| SW1, SW2 | 2 | Tactile switch 6×3.5 mm, right angle | SMD | EN and BOOT | 0.3 |
| TP1–TP14 | 14 | Test pad 1.5 mm | bare copper | all 20 edge pins' nets that the card uses, +5 V (pin 6), 3.3 V, GND | 0 |
| PCB | 1 | 2-layer 1.6 mm, ENIG, **hard-gold fingers with 20° bevel** on the tab, outline per hardware plan Task 1 | | JLCPCB/PCBWay: tick "gold fingers" and "bevel"; 5 pcs ≈ 30–50 incl. shipping | 8 each |
| Bracket | 1 | 37 × 120.72 × 1.42 mm plate, U-slots 5.84 deep 16.18 from each end, cut-outs for J1, J2, LEDs | 1.5 mm aluminium or steel | SendCutSend / a local laser shop from the plan's DXF, or reuse the AP9640 bracket | 5–15 |

Rough total for one card: about 40 USD in parts plus the PCB and bracket. Five PCBs and parts for two
cards land near 100–120 USD. The P4 module and the PCB with gold fingers are most of it.

Not on the list on purpose: no battery/RTC (ESPHome gets time from SNTP), no second UART header (the
Microlink protocol is the only one the card speaks), no DB9 pass-through parts (pins 7/12 untouched), no
status OLED. Pins 13/14 (power-on/off) are routed to test pads only.

## Can you assemble it yourself?

Yes, if you have a temperature-controlled iron with a fine tip, flux pen, tweezers and a magnifier. Every
part on the board was picked to be hand-solderable:

| Step | Difficulty | Why |
| --- | --- | --- |
| 0603 passives, SMA/SMB diodes, 1812 fuse, SOT/SOD parts | easy | standard hobby SMD |
| SC70-5 gates (U3, U4) | moderate | small but only 5 pins; tack one, then the rest |
| IP101GA LQFP-48, 0.5 mm pitch | moderate | drag-solder with plenty of flux, then check for bridges with a loupe. This is the one part worth practising on if you have never done fine-pitch |
| Waveshare P4 module, castellated 1 mm pitch | moderate | align, tack two corners, then run along each side; the pads are on the edge so it is easier than it looks |
| USB-C 16-pin | moderate | 0.5 mm pitch signal pins; only D+/D−, CC1/CC2, VBUS and GND matter, so a bridge on unused pins is harmless |
| MagJack, TSR buck, switches | easy | through-hole or large pads |

First board takes an evening. Bring-up order is in hardware plan Task 5: power section alone first
(check 3.3 V before fitting the module), then module, then PHY, then the UART parts.

If fine-pitch is not something you want to do, the split that works: order the PCB from JLCPCB with their
**economic PCBA** for everything in their LCSC catalogue (passives, diodes, gates, PHY, crystal, USB-C,
switches), and hand-solder what they cannot place: the Waveshare module, the MagJack and the TSR buck. That
leaves only easy and one moderate step (the module) for you, for roughly 15–30 USD extra per batch of five.
If you want zero soldering, swap to the design doc's IP101GRI and LMR33620 and ask the PCBA service to also
place the module as a customer-supplied part; expect a setup fee for that.

## Open items before ordering

- Confirm the Waveshare module's silicon revision from a sample you hold; it decides the ESPHome
  `engineering_sample` flag and whether the chosen GPIOs (47, 48, 53, 54) are free on that module variant.
- The RMII/MDI termination and PHY strap resistor count is set when the schematic is drawn (hardware plan
  Task 2) from the IP101GA datasheet; the BOM above estimates it.
- The gold-finger lengths (9.7 mm short, 10.5 mm long) come from the ruler photo with margin, per the design doc.
