"""Generate the aluminum case plates for GGSMDriver rev 1.

Writes ggsmdriver-case-top.dxf, ggsmdriver-case-bottom.dxf and the assembled
preview ggsmdriver-case.stl next to this file. Board outline and mounting
holes are read from ../../ggsmdriver.kicad_pcb.

Run: python3 make_case.py   (needs: pip install ezdxf shapely trimesh manifold3d mapbox_earcut)
"""
import math
import os
import re

import ezdxf
import shapely.geometry as sg
import trimesh

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, '..', '..', 'ggsmdriver.kicad_pcb')

PLATE_T = 3.0      # aluminum thickness
CORNER_R = 3.0     # plate outer corner radius
HOLE_D = 3.4       # M3 clearance, drilled
BOTTOM_GAP = 6.0   # bottom standoff length (clears THT pin tails)
TOP_GAP = 12.0     # top standoff length (clears header pins, terminal, cap)
PCB_T = 1.6

# Notch in the top plate over the screw terminal J3 so its screws stay
# reachable. KiCad courtyard of J3 is x 147.8-155, y 116.7-133.4.
NOTCH_X0 = 147.3   # board x where the notch starts (runs to the right edge)
NOTCH_Y = (116.0, 134.0)
NOTCH_R = 2.0      # inner corner radius (4 mm end mill)
NOTCH_EDGE_R = 1.0 # outer corners where the notch meets the plate edge

B = math.tan(math.radians(90) / 4)  # bulge of a 90 degree arc


def board():
    s = open(PCB).read()
    xs, ys = [], []
    for m in re.finditer(r'\(gr_(?:line|arc) \(start ([-\d.]+) ([-\d.]+)\).*?\(end ([-\d.]+) ([-\d.]+)\)'
                         r'[^()]*\(stroke[^)]*\)[^)]*\)\) \(layer "Edge.Cuts"\)', s):
        x0, y0, x1, y1 = map(float, m.groups())
        xs += [x0, x1]
        ys += [y0, y1]
    holes = []
    for blk in re.split(r'\n  \(footprint ', s)[1:]:
        if blk.startswith('"MountingHole:'):
            x, y = map(float, re.search(r'\n    \(at ([-\d.]+) ([-\d.]+)', blk).groups())
            holes.append((x, y))
    return min(xs), min(ys), max(xs), max(ys), holes


X0, Y0, X1, Y1, HOLES_PCB = board()
W, H = X1 - X0, Y1 - Y0


def to_plate(x, y):
    """KiCad board coords (y down) -> plate coords (origin bottom-left, y up)."""
    return x - X0, Y1 - y


HOLES = [to_plate(x, y) for x, y in HOLES_PCB]
nx = NOTCH_X0 - X0
ny0, ny1 = sorted(to_plate(0, y)[1] for y in NOTCH_Y)


def outline(notch):
    r = CORNER_R
    # (x, y, bulge of the segment leaving this vertex), counter-clockwise
    pts = [(r, 0, 0), (W - r, 0, B)]
    if notch:
        e, n = NOTCH_EDGE_R, NOTCH_R
        pts += [(W, r, 0), (W, ny0 - e, B), (W - e, ny0, 0), (nx + n, ny0, -B),
                (nx, ny0 + n, 0), (nx, ny1 - n, -B), (nx + n, ny1, 0), (W - e, ny1, B),
                (W, ny1 + e, 0)]
    else:
        pts += [(W, r, 0)]
    pts += [(W, H - r, B), (W - r, H, 0), (r, H, B), (0, H - r, 0), (0, r, B)]
    return pts


def write_dxf(name, notch):
    doc = ezdxf.new('R2010', setup=False)
    doc.units = ezdxf.units.MM
    doc.header['$INSUNITS'] = 4
    doc.layers.add('CUT_OUTLINE', color=1)
    doc.layers.add('DRILL', color=3)
    msp = doc.modelspace()
    msp.add_lwpolyline([(x, y, 0, 0, b) for x, y, b in outline(notch)], format='xyseb',
                       close=True, dxfattribs={'layer': 'CUT_OUTLINE'})
    for x, y in HOLES:
        msp.add_circle((x, y), HOLE_D / 2, dxfattribs={'layer': 'DRILL'})
    doc.saveas(os.path.join(HERE, name))


def plate_poly(notch):
    p = sg.box(CORNER_R, CORNER_R, W - CORNER_R, H - CORNER_R).buffer(CORNER_R, 32)
    if notch:
        cut = sg.box(nx, ny0, W + 5, ny1)
        p = p.difference(cut).buffer(NOTCH_R, 32).buffer(-NOTCH_R, 32)  # round inner corners
    for x, y in HOLES:
        p = p.difference(sg.Point(x, y).buffer(HOLE_D / 2, 32))
    return p


def ext(poly, z0, h):
    m = trimesh.creation.extrude_polygon(poly, h)
    m.apply_translation((0, 0, z0))
    return m


def hexagon(x, y, af=5.5):
    r = af / math.sqrt(3)
    return sg.Polygon([(x + r * math.cos(math.radians(a)), y + r * math.sin(math.radians(a)))
                       for a in range(0, 360, 60)])


def part_box(x0, y0, x1, y1, h, z0):
    (a, b), (c, d) = to_plate(x0, y0), to_plate(x1, y1)
    return ext(sg.box(min(a, c), min(b, d), max(a, c), max(b, d)), z0, h)


def write_stl(name):
    z_pcb = PLATE_T + BOTTOM_GAP
    z_top = z_pcb + PCB_T
    z_lid = z_top + TOP_GAP
    m = [ext(plate_poly(False), 0, PLATE_T), ext(plate_poly(True), z_lid, PLATE_T)]
    pcb = sg.box(1, 1, W - 1, H - 1).buffer(1, 16)
    for x, y in HOLES:
        pcb = pcb.difference(sg.Point(x, y).buffer(1.6, 32))
        m += [ext(hexagon(x, y), PLATE_T, BOTTOM_GAP), ext(hexagon(x, y), z_top, TOP_GAP),
              ext(sg.Point(x, y).buffer(2.85, 32), -1.65, 1.65),          # button heads
              ext(sg.Point(x, y).buffer(2.85, 32), z_lid + PLATE_T, 1.65)]
    m.append(ext(pcb, z_pcb, PCB_T))
    # Tallest parts (rough boxes, heights in mm above the board)
    m += [part_box(148.4, 116.7, 154.6, 133.4, 9.0, z_top),    # J3 screw terminal
          part_box(120.1, 100.5, 137.9, 103.1, 8.5, z_top),    # J4 1x7 header + pins
          part_box(100.6, 120.7, 108.3, 128.3, 5.0, z_top),    # J6 STDC14 header
          part_box(122.6, 104.0, 129.2, 110.0, 4.5, z_top),    # L1
          part_box(125.5, 137.5, 134.5, 145.0, 3.3, z_top),    # J5 USB-C
          part_box(122.5, 121.5, 129.5, 128.5, 1.6, z_top),    # U1
          part_box(110.4, 122.0, 115.6, 127.0, 1.5, z_top),    # SW1
          part_box(110.4, 138.0, 115.6, 143.0, 1.5, z_top)]    # SW2
    cx, cy = to_plate(139.0, 109.0)
    m.append(ext(sg.Point(cx, cy).buffer(3.15, 32), z_top, 5.8))  # C20 electrolytic
    trimesh.util.concatenate(m).export(os.path.join(HERE, name))


write_dxf('ggsmdriver-case-top.dxf', notch=True)
write_dxf('ggsmdriver-case-bottom.dxf', notch=False)
write_stl('ggsmdriver-case.stl')
print(f'plate {W:.1f} x {H:.1f} mm, holes {HOLES}, notch x {nx:.1f}-{W:.1f} y {ny0:.1f}-{ny1:.1f}')
