"""GGSMDriver schematic description, consumed by autopcb's schgen.

Run from the autopcb checkout, GGSM being this repo's path:
    python3 -m pcbai.schgen $GGSM/hardware/ggsmdriver.py $GGSM/kicad
(layout() feeds pcbai.pcbgen; see HANDOFF.md)

Every connection is a (pin -> net) entry; requirements.md is the spec.
"""
from pathlib import Path

from pcbai.pcbgen.board import Board
from pcbai.schgen.schematic import Schematic
from pcbai.schgen.symbolgen import box_symbol, write_library

HERE = Path(__file__).resolve().parent
LIB = HERE / "ggsmdriver.kicad_sym"

# Footprints (KiCad standard library names)
R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
LED0603 = "LED_SMD:LED_0603_1608Metric"

# JLC/LCSC numbers for generic parts, by value (checked 2026-10-16; all Basic since 2026-10-20).
LCSC = {
    "5.1k": "C23186", "1M": "C22935", "1k": "C21190", "10k": "C25804",
    "100k": "C25803", "13k": "C22797", "33k": "C4216", "20k": "C4184",
    "100nF": "C14663", "100nF 50V": "C14663", "10nF": "C57112",
    "22nF 50V": "C21122",
    "1uF": "C15849", "100pF": "C14858", "4.7uF": "C1779", "10uF": "C15850",
    "10uF 50V": "C13585", "22uF 25V": "C12891",
    # LEDs: all red KT-0603R, the one in-stock JLC Basic colour (Gustav, 2026-10-20).
    "PWR red": "C2286", "STATUS red": "C2286", "STEP red": "C2286",
    "DIR+ red": "C2286", "DIR- red": "C2286",
}


def tmc2209_symbol():
    # Pin numbers/names from the Adafruit breakout EAGLE library and the
    # BTT EBB36 V1.2 schematic (both agree). 3, 18, 25 and the pad are GND.
    return box_symbol(
        "TMC2209-LA", "U",
        left=[("22", "VS", "power_in"), ("28", "VS", "power_in"),
              ("15", "VCC_IO", "power_in"), ("8", "5VOUT", "power_out"),
              ("6", "VCP", "passive"), ("5", "CPI", "passive"),
              ("4", "CPO", "passive"), ("17", "VREF", "input"),
              ("13", "CLK", "input"), ("7", "SPREAD", "input"),
              ("9", "MS1_AD0", "input"), ("10", "MS2_AD1", "input"),
              ("20", "STDBY", "input")],
        right=[("16", "STEP", "input"), ("19", "DIR", "input"),
               ("2", "ENN", "input"), ("14", "PDN_UART", "bidirectional"),
               ("11", "DIAG", "output"), ("12", "INDEX", "output"),
               ("24", "OA1", "output"), ("21", "OA2", "output"),
               ("26", "OB1", "output"), ("1", "OB2", "output"),
               ("23", "BRA", "passive"), ("27", "BRB", "passive")],
        bottom=[("3", "GND", "power_in"), ("18", "GND", "power_in"),
                ("25", "GND", "power_in"), ("29", "EP", "power_in")],
        footprint="Package_DFN_QFN:QFN-28-1EP_5x5mm_P0.5mm_EP3.35x3.35mm",
    )


def build():
    write_library(LIB, [tmc2209_symbol()])
    s = Schematic("GGSMDriver", "ggsmdriver", libs=[LIB])

    def r(ref, value, a, b, block, fp=R0603, **f):
        f.setdefault("LCSC", LCSC.get(value, ""))
        s.add(ref, "Device:R", value, fp, {"1": a, "2": b}, block, f)

    def c(ref, value, a, b, block, fp=C0603, **f):
        f.setdefault("LCSC", LCSC.get(value, ""))
        s.add(ref, "Device:C", value, fp, {"1": a, "2": b}, block, f)

    def led(ref, value, anode, cathode, block):
        s.add(ref, "Device:LED", value, LED0603, {"A": anode, "K": cathode},
              block, {"LCSC": LCSC[value]})

    # --- Power in: 9-24 V on the first two poles of J3 (Connectors) ----
    s.pwr_flag("VIN")
    s.pwr_flag("GND")

    # --- Buck VIN -> +5V (TI TPS54202 5 V reference design) -------------
    b = "Buck 5V"
    s.add("U3", "Regulator_Switching:TPS54202DDC", "TPS54202DDCR",
          "Package_TO_SOT_SMD:TSOT-23-6",
          {"VIN": "VIN", "GND": "GND", "SW": "BUCK_SW", "BOOT": "BUCK_BST",
           "FB": "BUCK_FB"}, b, {"LCSC": "C191884"})  # EN: internal pull-up
    c("C1", "10uF 50V", "VIN", "GND", b, C1206)
    c("C2", "100nF 50V", "VIN", "GND", b)
    c("C3", "100nF", "BUCK_BST", "BUCK_SW", b)
    s.add("L1", "Device:L", "15uH", "Inductor_SMD:L_Bourns_SRN6045TA",
          {"1": "BUCK_SW", "2": "+5V"}, b, {"LCSC": "C467142"})
    r("R1", "100k", "+5V", "BUCK_FB", b)
    c("C4", "100pF", "+5V", "BUCK_FB", b)  # feed-forward; 100 pF is the nearest Basic C0G
    r("R2", "13k", "BUCK_FB", "GND", b)  # JLC Basic; gives 5.18 V
    c("C5", "22uF 25V", "+5V", "GND", b, C1206)
    c("C6", "22uF 25V", "+5V", "GND", b, C1206)
    s.pwr_flag("+5V")

    # --- USB-C: device, VBUS OR'd onto +5V ------------------------------
    b = "USB-C"
    s.add("J5", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C",
          "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
          {"VBUS": "VBUS", "GND": "GND", "CC1": "USB_CC1", "CC2": "USB_CC2",
           "D+": "USB_DP", "D-": "USB_DM", "SHIELD": "USB_SHIELD"}, b,
          {"LCSC": "C165948"})
    r("R3", "5.1k", "USB_CC1", "GND", b)
    r("R4", "5.1k", "USB_CC2", "GND", b)
    r("R5", "1M", "USB_SHIELD", "GND", b)
    c("C7", "10nF", "USB_SHIELD", "GND", b)
    s.add("U5", "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6",
          "Package_TO_SOT_SMD:SOT-23-6",
          {"I/O1": "USB_DM", "I/O2": "USB_DP", "VBUS": "VBUS", "GND": "GND"},
          b, {"LCSC": "C7519"})
    c("C8", "4.7uF", "VBUS", "GND", b, C0805)
    s.add("D1", "Device:D_Schottky", "SS14", "Diode_SMD:D_SMA",
          {"A": "VBUS", "K": "+5V"}, b, {"LCSC": "C2480"})
    s.pwr_flag("VBUS")

    # --- LDO +5V -> +3V3 -------------------------------------------------
    b = "LDO 3V3"
    s.add("U4", "Regulator_Linear:XC6206PxxxMR", "XC6206P332MR",
          "Package_TO_SOT_SMD:SOT-23",
          {"VI": "+5V", "VO": "+3V3", "GND": "GND"}, b, {"LCSC": "C5446"})
    c("C9", "1uF", "+5V", "GND", b)
    c("C10", "1uF", "+3V3", "GND", b)
    r("R6", "1k", "+3V3", "LED_PWR", b)
    led("D2", "PWR red", "LED_PWR", "GND", b)

    # --- MCU --------------------------------------------------------------
    b = "MCU"
    s.add("U1", "MCU_ST_STM32G0:STM32G0B1CBTx", "STM32G0B1CBT6",
          "Package_QFP:LQFP-48_7x7mm_P0.5mm",
          {"VDD": "+3V3", "VSS": "GND", "VBAT": "+3V3", "VREF+": "+3V3",
           "PF2": "NRST", "PA14": "BOOT0_SWCLK", "PA13": "SWDIO",
           "33": "USB_DM", "34": "USB_DP",
           "PA0": "ENC_A", "PA1": "ENC_B", "PA2": "TMC_TX", "PA3": "TMC_UART",
           "PA4": "IO1", "PA5": "IO2", "PA6": "TMC_STEP", "PA7": "TMC_DIR",
           "PB0": "TMC_EN", "PB1": "TMC_DIAG", "PB2": "LED_STAT_R",
           # PC6/PC7 here are VSS/VDDIO2 on the CBT6N, so either part fits.
           "PC6": "GND", "PC7": "+3V3"}, b, {"LCSC": "C2847904"})
    c("C11", "100nF", "+3V3", "GND", b)
    c("C12", "4.7uF", "+3V3", "GND", b, C0805)
    c("C13", "100nF", "+3V3", "GND", b)  # VREF+
    c("C14", "100nF", "+3V3", "GND", b)  # VBAT
    c("C15", "100nF", "NRST", "GND", b)
    r("R7", "10k", "BOOT0_SWCLK", "GND", b)
    r("R8", "1k", "LED_STAT_R", "LED_STAT", b)
    led("D3", "STATUS red", "LED_STAT", "GND", b)

    b = "Buttons and SWD"
    sw = "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A"
    s.add("SW1", "Switch:SW_Push", "BOOT", sw,
          {"1": "+3V3", "2": "BOOT0_SWCLK"}, b, {"LCSC": "C318884"})
    s.add("SW2", "Switch:SW_Push", "RESET", sw,
          {"1": "NRST", "2": "GND"}, b, {"LCSC": "C318884"})
    for i, net in enumerate(["+3V3", "SWDIO", "BOOT0_SWCLK", "NRST", "GND"], 1):
        s.add(f"TP{i}", "Connector:TestPoint", net,
              "TestPoint:TestPoint_Pad_D1.0mm", {"1": net}, b)

    # --- TMC2209 (Adafruit breakout circuit) --------------------------------
    b = "TMC2209"
    s.add("U2", "ggsmdriver:TMC2209-LA", "TMC2209-LA-T",
          "Package_DFN_QFN:QFN-28-1EP_5x5mm_P0.5mm_EP3.35x3.35mm",
          {"VS": "VIN", "VCC_IO": "+3V3", "5VOUT": "TMC_5V",
           "VCP": "TMC_VCP", "CPI": "TMC_CPI", "CPO": "TMC_CPO",
           "VREF": "GND", "CLK": "GND",  # VREF unused: current set over UART
           "SPREAD": "TMC_SPREAD",
           "MS1_AD0": "GND", "MS2_AD1": "GND", "STDBY": "GND",
           "STEP": "TMC_STEP", "DIR": "TMC_DIR", "ENN": "TMC_EN",
           "PDN_UART": "TMC_UART", "DIAG": "TMC_DIAG",
           "OA1": "MOT_A1", "OA2": "MOT_A2", "OB1": "MOT_B1", "OB2": "MOT_B2",
           "BRA": "TMC_BRA", "BRB": "TMC_BRB", "GND": "GND", "EP": "GND"},
          b, {"LCSC": "C2150710"})  # INDEX unused; -LA-T = -LA on tape & reel
    c("C16", "22nF 50V", "TMC_CPI", "TMC_CPO", b)
    c("C17", "100nF 50V", "TMC_VCP", "VIN", b)  # 100 nF per TMC2209 datasheet, Basic
    c("C18", "100nF 50V", "VIN", "GND", b)
    c("C19", "100nF 50V", "VIN", "GND", b)
    s.add("C20", "Device:C_Polarized", "22uF 50V", "Capacitor_SMD:CP_Elec_6.3x5.4",
          {"1": "VIN", "2": "GND"}, b, {"LCSC": "C176676"})  # Lelon VES220M1HTR-0605
    c("C21", "10uF", "TMC_5V", "GND", b, C0805)
    c("C22", "10uF", "+3V3", "GND", b, C0805)
    c("C23", "100nF", "+3V3", "GND", b)
    r("R9", "0.05R 1W", "TMC_BRA", "GND", b, R1206, LCSC="C154645")
    r("R10", "0.05R 1W", "TMC_BRB", "GND", b, R1206, LCSC="C154645")  # RALEC LR1206-21R050F4
    r("R12", "10k", "TMC_EN", "+3V3", b)  # driver off until MCU enables it
    s.add("JP1", "Jumper:SolderJumper_2_Open", "SPREAD",
          "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm",
          {"A": "TMC_SPREAD", "B": "+3V3"}, b)
    r("R13", "1k", "TMC_TX", "TMC_UART", b)  # datasheet 2-pin UART; RX is direct
    # Activity LEDs as on Adafruit: STEP, DIR high, DIR low (all red here).
    r("R14", "20k", "TMC_STEP", "LED_STEP", b)
    led("D4", "STEP red", "LED_STEP", "GND", b)
    r("R15", "20k", "LED_DIRH", "GND", b)
    led("D5", "DIR+ red", "TMC_DIR", "LED_DIRH", b)
    r("R16", "20k", "+3V3", "LED_DIRL", b)
    led("D6", "DIR- red", "LED_DIRL", "TMC_DIR", b)

    # --- Motor and expansion ------------------------------------------------
    b = "Connectors"
    # Power and motor on one 6-pole 2.54 mm terminal, as on the Adafruit breakout.
    # A1/A2 swapped so the motor tracks leave the TMC2209 without crossing.
    s.add("J3", "Connector:Screw_Terminal_01x06", "VIN GND A2 A1 B1 B2",
          "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MPT-0,5-6-2.54_1x06_P2.54mm_Horizontal",
          {"1": "VIN", "2": "GND", "3": "MOT_A2", "4": "MOT_A1", "5": "MOT_B1", "6": "MOT_B2"}, b,
          {"LCSC": "C474924"})  # Cixi Kefa KF128-2.54-6P
    s.add("J4", "Connector:Conn_01x07_Pin", "EXP",
          "Connector_PinHeader_2.54mm:PinHeader_1x07_P2.54mm_Vertical",
          {"1": "+5V", "2": "+3V3", "3": "GND", "4": "IO1", "5": "IO2",
           "6": "ENC_A", "7": "ENC_B"}, b)  # no LCSC: hand-soldered, left out of JLC assembly
    for i in range(1, 5):
        s.add(f"H{i}", "Mechanical:MountingHole", "M3",
              "MountingHole:MountingHole_3.2mm_M3", {}, b)
    return s


# --- PCB ------------------------------------------------------------------
# 4 layers: F.Cu / In2.Cu / B.Cu signals, In1.Cu a solid GND plane.
# Coordinates in mm from the board's top-left corner, x right, y down.
W, H = 55, 45


def tmc_power_stage(b):
    """Hand layout of the TMC2209 power side, after Adafruit's breakout: motor outputs as
    short wide tracks straight to J3, a via to the VIN plane at each VS pin, a via at each
    sense pin down to its sense resistor, VS caps beside the motor tracks. Coordinates are
    for U2 at (39, 22.5) rotated 270 (pins 22-28 face J3) and J3 at the right edge."""
    TX = 51.4  # x of J3's pads
    neck, wide = 0.25, 1.0  # pin pitch is 0.5 mm: leave each pin narrow, widen once clear
    # Coil B: pin 1 (top side) and pin 26; coil A: pin 24 and pin 21 (bottom side).
    b.track("MOT_B2", [(40.5, 20.05), (40.5, 18.8)], neck)
    b.track("MOT_B2", [(40.5, 18.8), (40.61, 18.69), (TX, 18.69)], wide)
    b.track("MOT_B1", [(41.45, 22.0), (42.6, 22.0), (43.4, 21.9)], neck)
    b.track("MOT_B1", [(43.4, 21.9), (44.07, 21.23), (TX, 21.23)], wide)
    b.track("MOT_A1", [(41.45, 23.0), (42.6, 23.0), (43.4, 23.1)], neck)
    b.track("MOT_A1", [(43.4, 23.1), (44.07, 23.77), (TX, 23.77)], wide)
    b.track("MOT_A2", [(40.5, 24.95), (40.5, 26.2)], neck)
    b.track("MOT_A2", [(40.5, 26.2), (40.61, 26.31), (TX, 26.31)], wide)
    # VS pins 28 and 22 straight down into the In2 VIN plane.
    b.track("VIN", [(41.45, 21.0), (41.9, 21.0), (41.9, 20.2)], neck)
    b.via(41.9, 20.2, "VIN")
    b.track("VIN", [(41.45, 24.0), (41.9, 24.0), (41.9, 24.8)], neck)
    b.via(41.9, 24.8, "VIN")
    # Sense pins 27 (BRB) and 23 (BRA): a via each; the router takes them to R10 / R9.
    b.track("TMC_BRB", [(41.45, 21.5), (42.2, 21.5), (42.7, 21.1)], neck)
    b.via(42.7, 21.1, "TMC_BRB")
    b.track("TMC_BRA", [(41.45, 23.5), (42.2, 23.5), (42.7, 23.9)], neck)
    b.via(42.7, 23.9, "TMC_BRA")
    b.place("R10", 42.0, 13.8)        # BRB sense, above coil B
    b.place("R9", 43.9, 31.0)         # BRA sense, below coil A
    b.place("C19", 44.6, 16.6)        # VS caps just outside the motor tracks
    b.place("C18", 44.6, 28.4)
    # Keep packed parts off the motor tracks.
    b.keepout(40.0, 17.9, 48.8, 19.5)
    b.keepout(41.8, 20.4, 48.8, 24.6)
    b.keepout(40.0, 25.6, 48.8, 27.1)


def buck_and_usb(b):
    """Hand-laid buck switch node and USB pair (2026-10-20 review)."""
    # Buck: SW (U3 pin 2) straight into L1, 0.8 mm; VIN and GND pins to their planes.
    b.track("BUCK_SW", [(21.14, 7.0), (23.825, 7.0)], 0.8)
    b.track("VIN", [(21.14, 6.05), (21.6, 6.05), (22.0, 5.65), (22.0, 5.2)], 0.4)
    b.via(22.0, 5.2, "VIN")
    b.track("GND", [(21.14, 7.95), (21.6, 7.95), (22.0, 8.35), (22.0, 8.8)], 0.4)
    b.via(22.0, 8.8, "GND")
    # USB D+/D-: one pair on F.Cu over the In1 GND plane, 0.25 mm / 0.15 mm gap (about
    # 90 ohm differential on JLC's 4-layer stackup, estimated), from U1 pins 34/33 through
    # the USBLC6 ESD pads (flow-through) to the USB-C pins.
    w = 0.25
    b.track("USB_DP", [(30.16, 23.25), (33.6, 23.25), (33.85, 23.5), (33.85, 28.0),
                       (30.75, 31.1), (30.75, 36.3)], w)
    b.track("USB_DM", [(30.16, 23.75), (31.7, 23.75), (31.95, 24.0), (31.95, 27.6),
                       (30.25, 29.3), (30.25, 36.3)], w)
    b.track("USB_DM", [(30.25, 35.7), (29.25, 35.7), (29.25, 36.3)], w)   # A7 to B7, above
    b.track("USB_DP", [(29.75, 37.3), (29.75, 37.9), (30.75, 37.9), (30.75, 37.3)], w)  # A6 to B6
    b.place("U5", 32.9, 26.0, 90)     # ESD: D- through pins 6/1, D+ through pins 4/3
    b.track("VBUS", [(32.9, 24.863), (32.9, 23.95)], 0.3)
    b.via(32.9, 23.95, "VBUS")        # VBUS and GND pins sit between the pair: a via each
    b.track("GND", [(32.9, 27.137), (32.75, 27.95)], 0.3)
    b.via(32.75, 27.95, "GND")
    b.track("VBUS", [(32.45, 36.3), (32.45, 35.4)], 0.3)
    b.via(32.45, 35.4, "VBUS")        # the pair cuts USB-C VBUS pin A9 off from A4
    b.place("R3", 28.2, 34.0)
    b.place("R4", 35.5, 34.3)
    b.keepout(30.9, 23.0, 34.3, 29.0)   # ESD chip and the pair beside U1
    b.keepout(29.9, 29.0, 31.1, 36.0)   # the pair down to USB-C
    # U1 pins 31 (+3V3) and 30 (GND) are boxed in by the pair: vias under the MCU body.
    b.track("+3V3", [(30.16, 24.75), (29.25, 24.75), (28.55, 24.05)], 0.25)
    b.via(28.55, 24.05, "+3V3")
    b.track("GND", [(30.16, 25.25), (29.25, 25.25), (28.55, 25.95)], 0.25)
    b.via(28.55, 25.95, "GND")


def layout(sch):
    b = Board(sch, W, H, layers=4, planes=["In1.Cu", "In2.Cu"])  # In1 GND, In2 VIN

    for ref, x, y in (("H1", 3.5, 3.5), ("H2", W - 3.5, 3.5),
                      ("H3", 3.5, H - 3.5), ("H4", W - 3.5, H - 3.5)):
        b.place(ref, x, y)
    b.edge("J3", "right", 25.04, 90)    # power + motor terminal: pins at y 31.39 (VIN) .. 18.69 (B2)
    b.edge("J5", "bottom", 30, 0)       # USB-C, opening faces down
    b.edge("J4", "top", 29, 90)         # expansion header
    b.place("U2", 39, 22.5, 270)      # TMC2209; motor and sense pins face J3, DIAG/UART face the MCU
    b.place("U1", 26, 25)             # MCU
    b.place("U3", 20, 7, 180)         # buck; SW pin faces L1
    b.place("L1", 25.9, 7)
    b.place("U4", 18.5, 14)           # LDO
    b.place("C20", 39, 9)             # VS bulk electrolytic
    b.place("SW1", 12.5, 24.5)        # BOOT
    b.place("SW2", 12.5, 40.5)        # RESET

    # Decoupling and support parts next to the pins they serve.
    for ref, target, pad in (
            ("C21", "U2", "8"), ("C16", "U2", "5"), ("C17", "U2", "6"),
            ("C23", "U2", "15"), ("C22", "U2", "15"),
            ("R13", "U2", "14"), ("R12", "U2", "2"), ("JP1", "U2", "7"),
            ("C11", "U1", "6"), ("C13", "U1", "5"), ("C14", "U1", "4"),
            ("C15", "U1", "10"), ("R7", "U1", "36"), ("C12", "U1", "6"),
            ("C3", "U3", "6"), ("C2", "U3", "3"), ("C1", "U3", "3"),
            ("R2", "U3", "4"), ("R1", "U3", "4"), ("C4", "U3", "4"),
            ("C9", "U4", "3"), ("C10", "U4", "2"), ("C5", "L1", "2"), ("C6", "L1", "2"),
            ("U5", "J5", "A6"), ("R3", "J5", "A5"), ("R4", "J5", "B5"),
            ("D1", "J5", "A4"), ("C8", "J5", "A4")):
        b.near(ref, target, pad)

    tmc_power_stage(b)
    buck_and_usb(b)
    # TMC2209 support parts pinned where the 2026-10-19 routing worked (region() moves
    # them whenever anything else changes, and the router then strands ENN).
    for ref, x, y in (("C16", 37.5, 18.0), ("C17", 37.5, 15.5),
                      ("R12", 46.0, 13.0), ("JP1", 46.0, 10.0)):
        b.place(ref, x, y)

    blocks = {}
    for p in sch.parts:
        if p.footprint and b._rects.get(p.ref) is None:
            blocks.setdefault(p.block, []).append(p.ref)
    # Indicator LEDs in one column at the top-right, labelled; their resistors below J3.
    leds = ["R6", "D2", "R8", "D3", "R14", "D4", "R15", "D5", "R16", "D6"]
    for i, (ref, label) in enumerate((("D2", "PWR"), ("D3", "STAT"), ("D4", "STEP"),
                                      ("D5", "DIR+"), ("D6", "DIR-"))):
        y = 8.0 + 1.8 * i
        b.place(ref, 49.9, y)
        b.board.FindFootprintByReference(ref).Reference().SetVisible(False)
        b.text(53.1, y, label, 0.8)
    for ref, x, y in (("R6", 39.7, 34.6), ("R8", 39.7, 36.6), ("R14", 42.9, 34.6), ("R15", 46.1, 34.6), ("R16", 46.1, 36.6),):
        b.place(ref, x, y)
    b.region(16, 3, 35, 19, [r for r in blocks["Buck 5V"] + blocks["LDO 3V3"] if r not in leds])
    b.region(32, 4, 48, 38, [r for r in blocks["TMC2209"] if r not in leds])
    b.region(16, 17, 34, 35, [r for r in blocks["MCU"] if r not in leds])
    b.region(8, 33, 47, 45, blocks["USB-C"])
    b.region(7, 21, 24, 45, blocks["Buttons and SWD"])

    # Thermal vias under the TMC2209 pad into the In1 GND plane.
    ux, uy = b._anchor("U2")
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            b.via(ux + dx, uy + dy, "GND")
    b.tie("U2", ["3", "9", "10", "13", "18", "20", "25"], "29")  # GND pins to the pad

    b.netclass("Power", ["VIN", "+5V", "VBUS", "TMC_5V"], track=0.5)  # +3V3 stays 0.2: MCU pins
    b.netclass("USB", ["USB_DP", "USB_DM"], track=0.25, clearance=0.15)
    b.netclass("Motor", ["MOT_A1", "MOT_A2", "MOT_B1", "MOT_B2",
                         "TMC_BRA", "TMC_BRB"], track=0.8)
    b.zone("GND", layers=("F.Cu", "In1.Cu", "B.Cu"))
    b.zone("VIN", layers=("In2.Cu",))  # motor supply plane: VS pins, caps, jack, buck
    for net in ("GND", "VIN"):
        b.stitch(net)              # a plane via at every pad before routing...
        b.stitch_nets.append(net)  # ...and again for any pad the router left on an island
    b.text(W / 2, H - 1.2, "GGSMDriver", 1.0, "B.SilkS")
    return b
