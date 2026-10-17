"""Parametric sensor box for the Adafruit ICM-20948 breakout (product 4554).

Run headless:   freecadcmd sensor_box.py
Outputs (next to this script): sensor_box.FCStd, sensor_box_base.{step,stl},
sensor_box_lid.{step,stl}. The lid STL is flipped top-down, ready to print.

Axes: +Y = boat forward (matches the sensor's Y arrow), +Z = up.
The cable gland sits in the aft (-Y) wall, next to the VIN/GND/SDA/SCL pads.
Use only non-magnetic hardware: brass heat-set inserts, brass or nylon screws.
"""
import os
import Part
import FreeCAD as App
from FreeCAD import Vector as V

# ---- Breakout board (Adafruit 4554, 1.0" x 0.7") -- verify with calipers ----
PCB_W = 25.4          # X
PCB_L = 17.8          # Y
PCB_T = 1.6
HOLE_DX = 20.32       # mounting hole spacing in X (0.8")
HOLE_DY = 12.7        # mounting hole spacing in Y (0.5")

# ---- Cable gland (default M8x1.25, cable 2-5 mm) ----
# PG7 instead: GLAND_HOLE_D = 12.7, GLAND_NUT_AC = 22.0
GLAND_HOLE_D = 8.3
GLAND_NUT_AC = 14.4   # locknut across corners

# ---- Box ----
WALL = 2.4
FLOOR = 2.0
CLEAR = 1.0           # gap board <-> wall
CABLE_ZONE = 11.0     # room between board and gland wall for wires + locknut
STANDOFF_H = 4.0
STANDOFF_D = 6.0
INSERT_D = 3.6        # hole for M2.5 brass heat-set insert
INSERT_DEPTH = 5.0

LID_T = 2.5
LIP_DEPTH = 2.0
LIP_T = 1.2
LIP_GAP = 0.2         # per side, lip <-> inner wall
BOSS_R = 4.0          # external corner bosses for lid screws
BOSS_OFFSET = 0.5     # boss centre outside the box corner
LID_SCREW_D = 2.9     # clearance for M2.5

EAR_LEN = 11.0        # mounting flanges on +/-X sides
EAR_W = 16.0
EAR_T = 4.0
SLOT_D = 4.5          # M4 screw; slot along Y allows small yaw adjustment
SLOT_LEN = 8.0

ARROW_DEPTH = 0.6

# ---- Derived ----
IX = PCB_W + 2 * CLEAR
IY = PCB_L + 2 * CLEAR + CABLE_ZONE
gland_z = GLAND_NUT_AC / 2 + 1.0                       # above floor
IH = max(14.0, gland_z + GLAND_NUT_AC / 2 + LIP_DEPTH + 0.5)
OX, OY, OZ = IX + 2 * WALL, IY + 2 * WALL, FLOOR + IH
pcb_cy = IY / 2 - CLEAR - PCB_L / 2                    # board pushed forward
boss_xy = [(sx * (OX / 2 + BOSS_OFFSET), sy * (OY / 2 + BOSS_OFFSET))
           for sx in (-1, 1) for sy in (-1, 1)]


def box(x, y, z, cx=0, cy=0, z0=0):
    return Part.makeBox(x, y, z, V(cx - x / 2, cy - y / 2, z0))


def cyl(r, h, x, y, z0=0, d=V(0, 0, 1)):
    return Part.makeCylinder(r, h, V(x, y, z0), d)


def outline(h, z0=0):
    s = box(OX, OY, h, z0=z0)
    for x, y in boss_xy:
        s = s.fuse(cyl(BOSS_R, h, x, y, z0))
    return s


def slot(cx, length, width, h):
    s = box(width, length - width, h, cx=cx)
    s = s.fuse(cyl(width / 2, h, cx, (length - width) / 2))
    return s.fuse(cyl(width / 2, h, cx, -(length - width) / 2))


def make_base():
    b = outline(OZ)
    for sx in (-1, 1):
        cx = sx * (OX / 2 + EAR_LEN / 2)
        ear = box(EAR_LEN + WALL, EAR_W, EAR_T, cx=cx - sx * WALL / 2)
        ear = ear.cut(slot(sx * (OX / 2 + EAR_LEN / 2 + 0.5), SLOT_LEN, SLOT_D, EAR_T))
        b = b.fuse(ear)
    b = b.cut(box(IX, IY, IH + 1, z0=FLOOR))
    for x, y in boss_xy:
        b = b.cut(cyl(INSERT_D / 2, INSERT_DEPTH, x, y, OZ - INSERT_DEPTH))
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * HOLE_DX / 2, pcb_cy + sy * HOLE_DY / 2
            post = cyl(STANDOFF_D / 2, STANDOFF_H, x, y, FLOOR)
            post = post.cut(cyl(INSERT_D / 2, STANDOFF_H, x, y, FLOOR))
            b = b.fuse(post)
    b = b.cut(cyl(GLAND_HOLE_D / 2, WALL + 2, 0, -OY / 2 - 1, FLOOR + gland_z, V(0, 1, 0)))
    return b.removeSplitter()


def make_lid():
    lid = outline(LID_T, z0=OZ)
    lx, ly = IX - 2 * LIP_GAP, IY - 2 * LIP_GAP
    lip = box(lx, ly, LIP_DEPTH, z0=OZ - LIP_DEPTH)
    lip = lip.cut(box(lx - 2 * LIP_T, ly - 2 * LIP_T, LIP_DEPTH, z0=OZ - LIP_DEPTH))
    # notch the lip above the gland locknut
    lip = lip.cut(box(GLAND_NUT_AC + 2, LIP_T * 3, LIP_DEPTH, cy=-ly / 2, z0=OZ - LIP_DEPTH))
    lid = lid.fuse(lip)
    for x, y in boss_xy:
        lid = lid.cut(cyl(LID_SCREW_D / 2, LID_T, x, y, OZ))
    # forward arrow engraved in the top, over the sensor
    top = OZ + LID_T - ARROW_DEPTH
    shaft = box(3.0, 10.0, ARROW_DEPTH, cy=pcb_cy - 3, z0=top)
    tip_y = pcb_cy + 9
    head = Part.Face(Part.makePolygon([V(-6, tip_y - 7, top), V(6, tip_y - 7, top),
                                       V(0, tip_y, top), V(-6, tip_y - 7, top)]))
    lid = lid.cut(shaft.fuse(head.extrude(V(0, 0, ARROW_DEPTH))))
    return lid.removeSplitter()


def board_dummy():
    return box(PCB_W, PCB_L, PCB_T, cy=pcb_cy, z0=FLOOR + STANDOFF_H)


here = os.path.dirname(os.path.abspath(__file__))
base, lid = make_base(), make_lid()

doc = App.newDocument("sensor_box")
doc.addObject("Part::Feature", "Base").Shape = base
doc.addObject("Part::Feature", "Lid").Shape = lid
doc.addObject("Part::Feature", "ICM20948_board").Shape = board_dummy()
doc.recompute()
doc.saveAs(os.path.join(here, "sensor_box.FCStd"))

lid_print = lid.copy()
lid_print.rotate(V(0, 0, 0), V(1, 0, 0), 180)
lid_print.translate(V(0, 0, OZ + LID_T))
for name, shape, printable in (("base", base, base), ("lid", lid, lid_print)):
    shape.exportStep(os.path.join(here, f"sensor_box_{name}.step"))
    m = printable.tessellate(0.02)
    import Mesh
    Mesh.Mesh([[m[0][i] for i in tri] for tri in m[1]]).write(
        os.path.join(here, f"sensor_box_{name}.stl"))

print(f"inner {IX:.1f} x {IY:.1f} x {IH:.1f} mm, outer {OX:.1f} x {OY:.1f} x {OZ + LID_T:.1f} mm "
      f"(+{EAR_LEN:.0f} mm flanges each side), gland centre {FLOOR + gland_z:.1f} mm above base")
for n, s in (("base", base), ("lid", lid)):
    print(f"{n}: valid={s.isValid()} solids={len(s.Solids)} volume={s.Volume / 1000:.1f} cm3")
