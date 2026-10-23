# GGSMDriver 3D-printed case

A slim snap-fit case for GGSMDriver rev 1 (9660d0a). No screws hold the covers
together. Modelled in Rhino (`ggsmdriver_case.3dm`); the Rhino MCP server config
for Claude Code is in the repo's `.mcp.json`. Outside size is 58.6 x 48.6 x 17.7 mm.

| File | What |
|---|---|
| `ggsmdriver_case.3dm` | Rhino source: layers `board` (KiCad STEP of the board, locked), `case_bottom`, `case_top` |
| `ggsmdriver-case-bottom.step` | Bottom cover |
| `ggsmdriver-case-top.step` | Top cover |
| `ggsmdriver-case.step` | Both covers and the board, as assembled |
| `ggsmdriver-case-bottom-print.stl` | Bottom cover for printing, floor down |
| `ggsmdriver-case-top-print.stl` | Top cover for printing, flipped lid down |

Coordinates: mm, the same as a KiCad 10 STEP export of the board (KiCad x, −KiCad y):
board x 100 to 155, y −145 to −100, board bottom at z 0, board top at z 1.6. The
covers go from z −5.5 (underside) to z 12.2 (lid top). The print STLs are moved to
start at 0, 0, 0.

## Bottom cover

- 2.5 mm floor, exactly the 55 x 45 mm board outline, so nothing sticks out past
  the board edge and USB-C stays reachable with only the bottom fitted.
- Four 3 mm-high, 6 mm bosses under the board's M3 holes, with blind 2.5 mm pilot
  holes for **4 x M3 x 6 self-tapping pan-head screws** (e.g. for plastic). The
  board's through-hole pin tails reach 1.9 mm below the board, so there is 1.1 mm
  to spare.
- Two 3.4 mm holes for mounting the case (M3, or a #4/#6 wood screw), 31 mm apart
  on the centre line. The screw heads sit on the floor under the board, so mount
  the bottom cover before fitting the board.
- A triangular snap groove along the middle 32 mm of each long side.

## Top cover

- 1.6 mm walls, 1.5 mm lid, 0.2 mm clearance around the board. 0.6 mm headroom
  over the tallest parts (J3 terminal and J4 header, 8.5 mm above the board).
- Snap ridges (0.5 mm, 30 mm long) on both long walls click into the bottom
  cover's grooves. To open, flex a long wall outwards.
- Right wall, both open at the bottom so the cover slides down over wired parts:
  - 18 mm cutout at J3 for the motor/power wires, up to 5.5 mm above the board.
  - 6 mm slot near the back corner for the optional J4 GPIO wires (run them along
    the back edge between J4 and C20), up to 4 mm above the board.
- 10 x 10 mm opening in the lid over the TMC2209 (U2) for an optional heatsink.
  It fits the usual 9 x 9 mm stick-on TMC2209 heatsinks. Use a thermal pad of at
  least 0.5 mm: one 0805 capacitor reaches 0.1 mm under the heatsink's edge and is
  0.3 mm taller than the chip.
- USB-C, J6 and the buttons are covered.

## Printing

Print the bottom cover floor-down and the top cover lid-down (as in the print
STLs). No supports needed. PETG flexes better than PLA for the snap.
