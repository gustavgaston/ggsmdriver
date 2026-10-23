# GGSMDriver 3D-printed case

A slim snap-fit case for when the board is mounted. No screws hold the covers
together. Fits rev 1 (9660d0a); the board itself is unchanged. Outside size is
58.6 x 48.6 x 17.7 mm.

| File | What |
|---|---|
| `ggsmdriver-case-bottom.stl` / `.step` | Bottom cover |
| `ggsmdriver-case-top.stl` / `.step` | Top cover |
| `ggsmdriver-case.step` | Both covers in place, for import into Rhino next to the board model |
| `make_case.py` | Regenerates everything (`pip install cadquery`; the dimensions are at the top of the file) |

Coordinates: mm, origin at the board's bottom-left corner seen from above
(KiCad x − 100, 145 − KiCad y), and z = 0 under the bottom cover. The board top is
at z 7.1. To line the case up with a KiCad 10 STEP export of the board (which uses
KiCad coordinates, Y flipped), move the case by (100, −145, 5.5).

## Bottom cover

- 2.5 mm floor, exactly the 55 x 45 mm board outline, so nothing sticks out past
  the board edge and USB-C stays reachable with only the bottom fitted.
- Four 3 mm-high, 6 mm bosses under the board's M3 holes, with 2.5 mm pilot holes
  for **4 x M3 x 6 self-tapping pan-head screws** (e.g. for plastic). The board's
  through-hole pin tails reach 1.9 mm below the board, so there is 1.1 mm to spare.
- Two 3.4 mm holes for mounting the case (M3, or a #4/#6 wood screw), 31 mm apart
  on the centre line. The screw heads sit on the floor under the board (2.4 mm max
  head height), so mount the bottom cover before fitting the board.
- A snap groove along the middle 32 mm of each long side.

## Top cover

- 1.6 mm walls, 1.5 mm lid, 0.2 mm clearance around the board. 0.6 mm headroom
  over the tallest parts (J3 terminal and J4 header, both 8.5 mm above the board,
  measured from the KiCad 10 3D models).
- Snap ridges (0.5 mm) on both long walls click into the bottom cover's grooves.
  To open, flex a long wall outwards.
- Cable cutout in the right wall for the motor/power wires on J3 (18 mm wide,
  open at the bottom so the cover slides down over wired terminals).
- Thin 20 x 2 mm slot in the back wall at J4 for the optional GPIO wires.
- USB-C, J6 and the buttons are covered.

## Printing

Print the bottom cover floor-down and the top cover lid-down, no supports needed
except the bridge over the cable cutout and the slot (short bridges). PETG flexes
better than PLA for the snap.
