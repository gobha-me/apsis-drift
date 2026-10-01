"""Independent craft09 source mechanism channels. BSD-3-Clause.

Importing this module never opens or saves a file. The caller supplies the exact
source to prepare(). Existing clocks, gear, ramp and other mechanisms are frozen.
"""
import hashlib
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

from wayfarer_operating_spec import CHANNEL_IDS, MOTION_RIGS, SOURCE_SHA256

CONVERT = Matrix(((1, 0, 0, 0), (0, 0, 1, 0),
                  (0, -1, 0, 0), (0, 0, 0, 1)))
INNER_AUTHORED_FRACTION = .55
LOCK_WITHDRAWAL_METRES = .070
LINK_INWARD_METRES = .025
BOLT_CIRCLE_ANGLE_RADIANS = math.radians(7.5)


def columns(matrix):
    return [list(matrix.col[i][:3]) for i in range(4)]


def ancestry(obj):
    result = []
    while obj is not None:
        result.append(obj.name)
        obj = obj.parent
    return result


class OperatingPoseController:
    @classmethod
    def prepare(cls, source: Path):
        source = Path(source).resolve()
        if hashlib.sha256(source.read_bytes()).hexdigest() != SOURCE_SHA256:
            raise ValueError("Operating source does not match the selected craft09")
        bpy.ops.wm.open_mainfile(filepath=str(source))
        scene = bpy.context.scene
        rigs = {group: bpy.data.objects[name] for name, group in MOTION_RIGS.items()}
        scene.frame_set(1)
        bpy.context.view_layer.update()
        board = {group: (obj.location.copy(), obj.rotation_euler.copy(), obj.scale.copy())
                 for group, obj in rigs.items()}
        scene.frame_set(180)
        bpy.context.view_layer.update()
        dock = {group: (obj.location.copy(), obj.rotation_euler.copy(), obj.scale.copy())
                for group, obj in rigs.items()}
        scene.frame_set(100)
        bpy.context.view_layer.update()
        return cls(source, rigs, board, dock)

    def __init__(self, source, rigs, board, dock):
        # Gear sampling may still query the authored frames before freeze().
        self.source = source
        self.rigs = rigs
        self.board = board
        self.dock = dock
        self.frozen = False

    def freeze(self):
        if self.frozen:
            raise RuntimeError("Operating source is already frozen")
        bpy.context.scene.frame_set(100)
        bpy.context.view_layer.update()
        for obj in bpy.context.scene.objects:
            if obj.animation_data:
                obj.animation_data_clear()
        self.rigs["seat_carriage"].location.y += .08
        self.rigs["seat_lift"].location.z += .18
        bpy.context.view_layer.update()
        # These bounded derivative fit corrections affect existing components,
        # never the read-only master. Render and contact producers share them.
        operations = []

        def operation(identity, reason, objects, parameters, mutate=None):
            records = []
            for obj in objects:
                original = obj.matrix_local.copy()
                location = obj.location.copy()
                if mutate:
                    mutate(obj)
                bpy.context.view_layer.update()
                records.append({"source_object": obj.name,
                                "source_parent": obj.parent.name if obj.parent else None,
                                "original_rest_local_transform": columns(original),
                                "operating_rest_local_transform": columns(obj.matrix_local),
                                "local_translation_metres": list(obj.location - location)})
            operations.append({"id": identity, "reason": reason,
                               "source_objects": records, "parameters": parameters})

        links = [bpy.data.objects[f"AFT01 | hatch hinge link {side} {height}"]
                 for side in (-1, 1) for height in (-.2, .2)]

        def move_link(obj):
            side = -1 if obj.parent == self.rigs["roof_port"] else 1
            obj.location.x -= side * LINK_INWARD_METRES

        operation("roof_hinge_link_clearance",
                  "Move four existing internal links inward to clear the pressure ceiling through the authored hatch rotation.",
                  links, {"inward_metres": LINK_INWARD_METRES}, move_link)
        operation("inner_door_stroke",
                  "Limit the four authored leaf displacements to the verified useful aperture before inlays reach fixed pressure-frame members.",
                  [self.rigs[group] for group in ("inner_port_outer", "inner_port_inner",
                                                 "inner_starboard_outer", "inner_starboard_inner")],
                  {"authored_fraction": INNER_AUTHORED_FRACTION})
        operation("seat_lock_withdrawal",
                  "Withdraw the existing lock on its source local X axis before swivel rotation and restore only after endpoint alignment.",
                  [self.rigs["seat_lock"]],
                  {"withdrawal_metres": LOCK_WITHDRAWAL_METRES,
                   "withdraw_begin": .50, "withdraw_end": .60,
                   "turn_begin": .60, "turn_end": .95,
                   "restore_begin": .95, "restore_end": 1.})
        bolt = bpy.data.objects["Swivel mounting bolt"]
        radius = math.hypot(bolt.location.x, bolt.location.y)
        if abs(radius - .24) > 1e-6 or abs(bolt.location.y) > 1e-6:
            raise ValueError("Source lock-axis mounting bolt no longer matches its pitch circle")

        def move_bolt(obj):
            obj.location.x = radius * math.cos(BOLT_CIRCLE_ANGLE_RADIANS)
            obj.location.y = radius * math.sin(BOLT_CIRCLE_ANGLE_RADIANS)

        operation("seat_lock_bolt_clearance",
                  "Relocate the single existing lock-axis fastener around its unchanged mounting pitch circle; the source housing has no dedicated hole or boss.",
                  [bolt], {"pitch_circle_radius_metres": radius,
                           "angle_radians": BOLT_CIRCLE_ANGLE_RADIANS}, move_bolt)
        self.derivative_corrections = {"schema_version": 1,
                                       "axes": "Blender source local",
                                       "operations": operations}
        # The pin translates on carriage-local X; this perpendicular interval
        # separation is invariant during carriage travel and lock withdrawal.
        carriage_inverse = self.rigs["seat_carriage"].matrix_world.inverted()

        def local_bounds(obj):
            points = [carriage_inverse @ obj.matrix_world @ Vector(point) for point in obj.bound_box]
            return ([min(point[axis] for point in points) for axis in range(3)],
                    [max(point[axis] for point in points) for axis in range(3)])

        bolt_lo, bolt_hi = local_bounds(bolt)
        pin_lo, pin_hi = local_bounds(bpy.data.objects["Swivel lock pin"])
        gap = bolt_lo[1] - pin_hi[1]
        bolts = [obj for obj in bpy.context.scene.objects if obj.name.startswith("Swivel mounting bolt")]
        neighbor_separation = math.inf
        for neighbor in bolts:
            if neighbor is bolt:
                continue
            lo, hi = local_bounds(neighbor)
            distance = math.sqrt(sum(max(0., lo[axis] - bolt_hi[axis],
                                         bolt_lo[axis] - hi[axis]) ** 2 for axis in range(3)))
            neighbor_separation = min(neighbor_separation, distance)
        housing_lo, housing_hi = local_bounds(bpy.data.objects["Fixed swivel housing"])
        overlap = housing_hi[0] - (pin_lo[0] + LOCK_WITHDRAWAL_METRES)
        if gap < .010 or neighbor_separation < .02 or overlap < .014 or len(bolts) != 12:
            raise ValueError("Derivative lock fit no longer meets measured source clearances")
        self.fit_measurements = {
            "pin_axis_carriage_local": [1., 0., 0.],
            "pin_bolt_perpendicular_interval_gap_metres": gap,
            "bolt_neighbor_aabb_separation_lower_bound_metres": neighbor_separation,
            "withdrawn_pin_housing_radial_overlap_metres": overlap,
            "mounting_bolt_count": len(bolts),
            "housing_mesh_vertices": len(bpy.data.objects["Fixed swivel housing"].data.vertices),
            "housing_mesh_polygons": len(bpy.data.objects["Fixed swivel housing"].data.polygons),
            "housing_modifiers": [modifier.type for modifier in bpy.data.objects["Fixed swivel housing"].modifiers]}
        self.local_rest = {
            group: (obj.location.copy(), obj.rotation_euler.copy(), obj.scale.copy())
            for group, obj in self.rigs.items()
        }
        self.world_rest = {group: obj.matrix_world.copy() for group, obj in self.rigs.items()}
        self.frozen = True
        return self

    def reset(self):
        if not self.frozen:
            raise RuntimeError("Freeze the source before requesting operating poses")
        for group, obj in self.rigs.items():
            obj.location, obj.rotation_euler, obj.scale = self.local_rest[group]
        bpy.context.view_layer.update()

    def apply(self, channel, progress, *, reset=True):
        if type(progress) not in (int, float) or not math.isfinite(progress) or not 0 <= progress <= 1:
            raise ValueError("Operating progress must be finite in [0,1]")
        if channel not in CHANNEL_IDS:
            raise ValueError("Unknown source operating channel")
        if not self.frozen:
            raise RuntimeError("Freeze the source before requesting operating poses")
        if reset:
            self.reset()
        if channel == "roof_transfer":
            hatch = min(1., progress / .40)
            swing = max(0., min(1., (progress - .40) / .30))
            extend = max(0., min(1., (progress - .70) / .30))
            for group in ("roof_port", "roof_starboard"):
                self.rigs[group].rotation_euler.y = self.dock[group][1].y * hatch
            group = "ladder_base"
            self.rigs[group].rotation_euler.z = (
                self.local_rest[group][1].z * (1 - swing) + self.dock[group][1].z * swing)
            group = "ladder_upper"
            self.rigs[group].location.z = (
                self.local_rest[group][0].z * (1 - extend) + self.dock[group][0].z * extend)
        elif channel == "inner_door":
            for group in ("inner_port_outer", "inner_port_inner",
                          "inner_starboard_outer", "inner_starboard_inner"):
                # The ground-boarding snapshot supplies only each inner leaf's
                # displacement. Its rear-ramp state is never copied or applied.
                self.rigs[group].location.x = self.board[group][0].x * progress * INNER_AUTHORED_FRACTION
        elif channel == "seat_boarding":
            slide = min(1., progress / .55)
            turn = max(0., min(1., (progress - .60) / .35))
            for group, axis in (("seat_carriage", 1), ("seat_lift", 2)):
                self.rigs[group].location[axis] = (
                    self.local_rest[group][0][axis] * (1 - slide) +
                    self.board[group][0][axis] * slide)
            self.rigs["seat_swivel"].rotation_euler.z = self.board["seat_swivel"][1].z * turn
            withdraw = max(0., min(1., (progress - .50) / .10))
            restore = max(0., min(1., (1. - progress) / .05))
            self.rigs["seat_lock"].location.x = (
                self.local_rest["seat_lock"][0].x + LOCK_WITHDRAWAL_METRES * withdraw * restore)
            # No independent entry-arm motion is invented from an old seat study.
        else:
            raise ValueError("Unknown source operating channel")
        bpy.context.view_layer.update()

    def deltas(self):
        inverse = CONVERT.inverted()
        return {group: Matrix.Identity(4) if obj.matrix_world == self.world_rest[group] else
                CONVERT @ obj.matrix_world @ self.world_rest[group].inverted() @ inverse
                for group, obj in self.rigs.items()}

    def matrices(self):
        return {group: columns(matrix) for group, matrix in self.deltas().items()}
