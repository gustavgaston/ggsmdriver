# GGSMDriver aluminum case

Two 3 mm aluminum plates, the same 55 x 45 mm footprint as the board, held on the
board's four M3 mounting holes with brass standoffs. Fits rev 1 (9660d0a); the board
itself is unchanged.

| File | What |
|---|---|
| `ggsmdriver-case-top.dxf` | Top plate, seen from above (terminal side on the right) |
| `ggsmdriver-case-bottom.dxf` | Bottom plate |
| `ggsmdriver-case.stl` | Assembled preview: plates, standoffs, screw heads, board and its tallest parts as rough blocks |
| `make_case.py` | Regenerates all three from `../../ggsmdriver.kicad_pcb` |

DXF units are mm, origin at the plate's bottom-left corner. Layer `CUT_OUTLINE` is the
profile (one closed polyline with arcs), layer `DRILL` the holes.

## Machining (4 mm end mill)

- **Drill:** the four 3.4 mm holes (M3 clearance) at 3.5 mm from each corner, 48 x 38 mm
  apart. They are smaller than the end mill, so drill them.
- **Mill:** the outer profile (3 mm corner radius) on both plates, and on the top plate
  the 7.7 x 18 mm notch over the screw terminal. Its inside corners are 2 mm radius,
  so a 4 mm end mill cuts them as drawn. No feature is narrower than 7.7 mm.

## Stack (bottom to top)

| Layer | Height | Notes |
|---|---|---|
| Bottom plate | 3 mm | |
| Standoff, M3 x 6 male-female | 6 mm | Male stud up through the board. Clears the through-hole pin tails of J3/J4 (~3 mm) |
| Board | 1.6 mm | |
| Standoff, M3 x 12 female-female | 12 mm | Sits on the stud. Clears the J4 header pins (8.5 mm), screw terminal J3 (~9 mm), C20 (5.8 mm), J6 (~5 mm) by 3 mm or more |
| Top plate | 3 mm | |

Overall about 25.6 mm tall plus screw heads.

## Parts list

| Qty | Part |
|---|---|
| 4 | M3 x 6 mm hex standoff, male-female, brass (5 or 5.5 mm hex, male thread 5-6 mm) |
| 4 | M3 x 12 mm hex standoff, female-female, brass |
| 8 | M3 x 6 mm button head screw (ISO 7380), stainless |

The stud of the 6 mm standoff passes the 1.6 mm board and goes about 4 mm into the
12 mm standoff, which leaves plenty of thread for the 3 mm the top screw needs. The
bottom screw goes 3 mm into the female end of the 6 mm standoff.

## Access with the case closed

- **Screw terminal J3** (motor + power): wires enter from the right side between the
  plates; the notch in the top plate leaves its screws reachable.
- **USB-C:** front edge, plates flush with the board edge, so any plug fits.
- **Headers J4 and J6** are covered, as asked. The ST-Link ribbon on J6 needs the top
  plate off (or a notch over J6 on the left edge, if that turns out to be annoying).
- **BOOT/RESET buttons** are covered: take the top plate off to enter DFU.

Component heights are from datasheets/typical values, not measured; check J3 and J4
with calipers after soldering. If a taller female header goes on J4, use 15 mm top
standoffs instead.
