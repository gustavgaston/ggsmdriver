# GGSMDriver — requirements and circuit spec

Milestone 1, 2026-10-16. All confirmed by Gustav.

## Confirmed decisions

| Topic | Decision |
|---|---|
| MCU | STM32G0B1CBT6 (LQFP48) |
| Supply | 9–24 V DC, one input used at a time: 5.5×2.1 mm barrel jack **or** 2-pole terminal, wired in parallel. No protection. |
| Supply use | Feeds TMC2209 VM directly; a buck makes 5 V, an LDO makes 3.3 V for MCU and TMC2209 VCC_IO. |
| USB | USB-C, USB 2.0 FS device, firmware upload via ROM DFU (BOOT button). USB alone powers the MCU when the supply is off. |
| Driver | TMC2209, Adafruit breakout as the base circuit. STEP/DIR/EN hardwired to the MCU, UART for setup, DIAG to the MCU. |
| Motor | 4-pole terminal for a 4-wire bipolar motor. |
| Expansion | Header with 2 × GPIO/ADC and 2 × timer pins for a quadrature encoder. |
| Build | JLCPCB assembly. |

## Blocks

### Power
- `VIN` (9–24 V) = barrel jack ∥ 2-pole terminal → TMC2209 VS, buck input.
- Buck TPS54202 (4.5–28 V in, 500 kHz) → `+5V`, using TI's 5 V reference
  design: 15 µH, 2 × 22 µF 25 V out, 10 µF 50 V + 0.1 µF in, 0.1 µF BOOT,
  FB 100 k / 13 k (5.18 V) with 75 pF across the top resistor, EN left to its pull-up.
- USB VBUS → Schottky → `+5V` (diode-OR as on BTT EBB36: when VIN is
  present the buck wins and the diode stops back-feeding the PC).
- `+5V` → XC6206P332 LDO (200 mA) → `+3V3` (MCU, TMC2209 VCC_IO, LEDs, header).
- Power LED on `+3V3`.

### MCU (STM32G0B1CBT6)
- VDD/VDDA, VBAT, VREF+ → `+3V3`; 100 nF per pin + 4.7 µF bulk.
- No crystal: USB runs from HSI48 with CRS.
- NRST: 100 nF to GND + RESET button to GND.
- PA14-BOOT0: BOOT button to `+3V3`, 10 k pull-down (shares SWCLK).
- SWD (PA13 SWDIO, PA14 SWCLK, NRST, 3V3, GND) on test pads, no header.
- Pins 30/31 tied to GND / `+3V3`: they are PC6/PC7 on CBT6 and
  VSS/VDDIO2 on CBT6N, so either part fits the same board.
- UCPD dead-battery pins 29/32 left open (as EBB36).

### USB-C
- 16-pin receptacle; CC1 and CC2 each 5.1 k to GND (so C-to-C cables power it).
- D+/D− to PA12/PA11 (pins 34/33), USBLC6-2SC6 ESD on the data pair.
- Shield to GND through 1 M ∥ 10 nF (as EBB36).

### TMC2209 (Adafruit base)
Copied from the Adafruit breakout:
- Sense resistors BRA/BRB 0.05 Ω (≈2 A max); 1 W 1206 parts instead of
  0.25 W, since 2 A RMS puts 0.2 W in each.
- CPI–CPO 22 nF 50 V; VCP–VS 0.22 µF 50 V.
- VS: 2 × 0.22 µF 50 V + 22 µF 50 V electrolytic.
- 5VOUT 10 µF; VCC_IO 10 µF + 0.22 µF on `+3V3`.
- VREF from a 10 k trimmer fed through 33 k from 5VOUT (current-limit pot).
- CLK to GND (internal clock).
- SPREAD via solder jumper (open = StealthChop; UART overrides anyway).
- STEP/DIR LEDs as on Adafruit.

Deviations (confirmed 2026-10-16):
- ENN pulled **up** 10 k to `+3V3`, so the motor stays off while the MCU
  boots or sits in DFU. Adafruit pulls it down (driver on at power-up).
- MS1_AD0, MS2_AD1 to GND: UART address 0 (ggstepper uses 0).
- STDBY to GND (as EBB36).
- UART per datasheet two-pin scheme: MCU TX → 1 k → PDN_UART, MCU RX direct.
- INDEX left unconnected.

### Connectors
- J1 barrel jack DC-005 (5.5 mm, 2.0 mm pin), J2 2-pole terminal 3.5 mm (VIN, GND).
- J3 4-pole terminal 3.5 mm: A1, A2, B1, B2.
- J4 1×7 2.54 mm header: +5V, +3V3, GND, IO1, IO2, ENC_A, ENC_B.
- J5 USB-C.

## Pin map (STM32G0B1CBT6)

| Pin | Port | Net | Why this pin |
|---|---|---|---|
| 11 | PA0 | ENC_A | TIM2_CH1 (32-bit timer, encoder mode) |
| 12 | PA1 | ENC_B | TIM2_CH2 |
| 13 | PA2 | TMC_TX | USART2_TX (→ 1 k → PDN_UART) |
| 14 | PA3 | TMC_RX | USART2_RX |
| 15 | PA4 | IO1 | ADC1_IN4 (also DAC1_OUT1) |
| 16 | PA5 | IO2 | ADC1_IN5 (also DAC1_OUT2) |
| 17 | PA6 | TMC_STEP | TIM3_CH1: hardware step pulses |
| 18 | PA7 | TMC_DIR | GPIO |
| 19 | PB0 | TMC_EN | GPIO (ENN, active low) |
| 20 | PB1 | TMC_DIAG | GPIO/EXTI1 |
| 21 | PB2 | LED_STATUS | GPIO |
| 33 | PA11 | USB_DM | USB |
| 34 | PA12 | USB_DP | USB |
| 35 | PA13 | SWDIO | SWD pad |
| 36 | PA14-BOOT0 | BOOT0/SWCLK | BOOT button, SWD pad |
| 10 | PF2-NRST | NRST | RESET button, SWD pad |

Pin functions checked against ST's STM32_open_pin_data
(`mcu/STM32G0B1C(B-C-E)Tx.xml`). USB on pins 33/34 matches BTT EBB36 V1.2.

## Parts (JLCPCB / LCSC)

Checked on jlcpcb.com / lcsc.com pages on 2026-10-16. Generic resistors and
capacitors get picked from JLC Basic parts in milestone 2.

| Ref | Part | LCSC | JLC type | Note |
|---|---|---|---|---|
| U1 | STM32G0B1CBT6 | C2847904 | ? | 137 in stock at LCSC |
| U2 | TMC2209-LA | C465949 | Extended | |
| U3 | TPS54202DDCR | C191884 | ? | 230k in stock |
| L1 | 15 µH 2.5 A SMNR6045-150MT | C467142 | Extended | 6×6 mm |
| U4 | XC6206P332MR-G | C5446 | Basic | |
| D1 | SS14 (VBUS OR-diode) | C2480 | Basic | |
| U5 | USBLC6-2SC6 | C7519 | Extended | |
| J5 | USB-C HRO TYPE-C-31-M-12 | C165948 | Extended | |
| J1 | DC-005 barrel jack, 2.0 mm pin | C16214 | ? | THT |
| J2 | KF350-3.5-2P terminal | C474892 | Extended | THT |
| J3 | KF350-3.5-4P terminal | C42400616 | Extended | THT |
| J4 | 1×7 2.54 mm header PZ254V-11-07P | C492406 | Extended | THT |
| RV1 | Bourns 3314J-1-103E 10 k trimmer | C36376 | Extended | |
| R_sense ×2 | 0.05 Ω 1 W 1206 FORT CUM1206-15FR050 | C5185600 | Extended | |
| C_bulk | 22 µF 50 V electrolytic 50CE22FS | C242075 | Extended | |
| SW1, SW2 | TS-1187A-B-A-B tactile | C318884 | ? | BOOT, RESET |

## Confirmed 2026-10-16 (milestone 1 review)

- ENN pulled up (driver off until the MCU enables it).
- DC-005 jack with 2.0 mm pin is fine.
- 3.5 mm terminals are fine.
- Header keeps both +5V and +3V3 (encoder voltage varies).
- Board about 50×40 mm with 4 M3 holes.

## Confirmed 2026-10-16 (milestone 2 review)

- Gustav ran ERC in KiCad 10: no errors.
- Keep the DC-005 jack despite its 12 V / 1 A datasheet rating.
- Buck feedback 100 k / 13 k (JLC Basic) → 5.18 V. Inductor C467142 15 µH 2.5 A.

## Confirmed 2026-10-16 (milestone 3)

- PCB: 4 layers, 55×45 mm (F.Cu / In1.Cu GND plane / In2.Cu / B.Cu), all parts on top.
  50×40 on 2 layers did not fit with single-sided assembly.
- Layout (generated, in review): TMC2209 turned so its motor/sense pins face the motor
  terminal J3 on the right; jack and supply terminal on the left; USB-C bottom;
  expansion header top; current trimmer RV1 bottom right. Motor and sense nets 0.8 mm,
  power nets 0.5 mm, signals 0.2 mm. 3×3 vias under the TMC2209 pad into the GND plane.
