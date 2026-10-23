"""3D-printed snap-fit case for GGSMDriver rev 1.

Writes ggsmdriver-case-bottom.step, ggsmdriver-case-top.step and the
assembly ggsmdriver-case.step (both covers in place) next to this file.
Coordinates: mm, origin at the board's bottom-left corner seen from above
(KiCad x - 100, 145 - KiCad y), z = 0 at the underside of the bottom cover.

Run: python3 make_case.py   (needs: pip install cadquery)
"""
import os

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))

# Board (rev 1, kicad/ggsmdriver.kicad_pcb)
BW, BH, BR, PCB_T = 55.0, 45.0, 1.0, 1.6
HOLES = [(3.5, 3.5), (51.5, 3.5), (3.5, 41.5), (51.5, 41.5)]  # M3, 3.2 mm
PART_H = 8.5         # tallest part above the board top (J3 terminal, J4 header; KiCad 10 models)

# Bottom cover
FLOOR = 2.5          # floor thickness, carries the snap groove in its side
BOSS_H = 3.0         # board standoff above the floor (pin tails reach 1.9 mm)
BOSS_D = 6.0
PILOT_D = 2.5        # self-tapping M3 pilot
MOUNT = [(12.0, 22.5), (43.0, 22.5)]  # holes for mounting the case
MOUNT_D = 3.4

# Top cover
WALL, LID, CLR, HEADROOM = 1.6, 1.5, 0.2, 0.6
GROOVE_Z, GROOVE_H, SNAP = 0.8, 1.2, 0.5   # snap ridge position/height/depth
SNAP_L = 30.0                              # ridge length on each long wall

# Cable cutout in the right wall for J3 (KiCad courtyard y 116.7-133.4)
CABLE_Y = (11.0, 29.0)
CABLE_TOP = 5.5      # above the board top
# Thin slot in the back wall for J4 wires (pins at KiCad x 121.38-136.62)
SLOT_X = (19.0, 39.0)
SLOT_Z = (2.0, 4.0)  # above the board top

Z_PCB = FLOOR + BOSS_H
Z_TOP = Z_PCB + PCB_T
Z_LID = Z_TOP + PART_H + HEADROOM
OW, OH = BW + 2 * (CLR + WALL), BH + 2 * (CLR + WALL)


def rrect(w, h, r, z0, t, x=0.0, y=0.0):
    return (cq.Workplane('XY').workplane(offset=z0).center(x + w / 2, y + h / 2)
            .rect(w, h).extrude(t).edges('|Z').fillet(r))


def ridge(y_face, inward, z0, length, depth, height):
    """Triangular ridge on a wall face at y = y_face, pointing inward (+1/-1 in y)."""
    pts = [(y_face, z0), (y_face + inward * depth, z0 + height / 2), (y_face, z0 + height)]
    return (cq.Workplane('YZ').workplane(offset=(BW - length) / 2)
            .polyline(pts).close().extrude(length))


def bottom():
    b = rrect(BW, BH, BR, 0, FLOOR)
    for x, y in HOLES:
        b = b.union(cq.Workplane('XY').workplane(offset=FLOOR).center(x, y).circle(BOSS_D / 2).extrude(BOSS_H))
        b = b.cut(cq.Workplane('XY').workplane(offset=1.0).center(x, y).circle(PILOT_D / 2).extrude(Z_PCB))
    for x, y in MOUNT:
        b = b.cut(cq.Workplane('XY').center(x, y).circle(MOUNT_D / 2).extrude(FLOOR))
    # snap grooves in the two long sides
    b = b.cut(ridge(0.0, 1, GROOVE_Z - 0.1, SNAP_L + 2, SNAP + 0.1, GROOVE_H + 0.2))
    b = b.cut(ridge(BH, -1, GROOVE_Z - 0.1, SNAP_L + 2, SNAP + 0.1, GROOVE_H + 0.2))
    return b


def top():
    o = -(CLR + WALL)
    t = rrect(OW, OH, BR + CLR + WALL, 0, Z_LID + LID, o, o)
    t = t.cut(rrect(BW + 2 * CLR, BH + 2 * CLR, BR + CLR, 0, Z_LID, -CLR, -CLR))
    t = t.union(ridge(-CLR, 1, GROOVE_Z, SNAP_L, SNAP, GROOVE_H))
    t = t.union(ridge(BH + CLR, -1, GROOVE_Z, SNAP_L, SNAP, GROOVE_H))
    # cable cutout, open at the bottom so the cover slides down over the wires
    t = t.cut(cq.Workplane('XY').center(BW + 1, sum(CABLE_Y) / 2)
              .rect(WALL + 4, CABLE_Y[1] - CABLE_Y[0]).extrude(Z_TOP + CABLE_TOP)
              .edges('|X').edges('>Z').fillet(1.5))
    # thin slot for J4 wires
    t = t.cut(cq.Workplane('XY').workplane(offset=Z_TOP + SLOT_Z[0])
              .center(sum(SLOT_X) / 2, BH + 1).rect(SLOT_X[1] - SLOT_X[0], WALL + 4)
              .extrude(SLOT_Z[1] - SLOT_Z[0]))
    return t


if __name__ == '__main__':
    b, t = bottom(), top()
    for name, part in (('bottom', b), ('top', t)):
        cq.exporters.export(part, os.path.join(HERE, f'ggsmdriver-case-{name}.step'))
    asm = cq.Assembly().add(b, name='bottom', color=cq.Color(0.3, 0.3, 0.3)) \
                       .add(t, name='top', color=cq.Color(0.9, 0.9, 0.9, 0.5))
    asm.save(os.path.join(HERE, 'ggsmdriver-case.step'))
    print(f'outside {OW:.1f} x {OH:.1f} x {Z_LID + LID:.1f} mm; board top at z {Z_TOP:.1f}, lid underside {Z_LID:.1f}')
