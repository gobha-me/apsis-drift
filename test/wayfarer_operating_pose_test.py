#!/usr/bin/env python3
"""Blender-only atomic operating-input regressions; masters remain read-only."""
import argparse
import copy
import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import bpy
from wayfarer_operating_blender import OperatingPoseController


def snapshot(controller):
    return {group: tuple(value for row in obj.matrix_world for value in row)
            for group, obj in controller.rigs.items()}


def refused(controller, command):
    before = snapshot(controller)
    try:
        command()
    except (ValueError, RuntimeError):
        pass
    else:
        raise AssertionError('Invalid operating command unexpectedly accepted')
    assert snapshot(controller) == before, 'Rejected command mutated authoritative source pose'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    digest = hashlib.sha256(args.source.read_bytes()).hexdigest()
    controller = OperatingPoseController.prepare(args.source)
    refused(controller, controller.reset)
    refused(controller, lambda: controller.apply('roof_transfer', 0))
    controller.freeze()
    correction = copy.deepcopy(controller.derivative_corrections)
    refused(controller, controller.freeze)
    controller.apply('roof_transfer', .3)
    for channel in ('roof_transfer', 'inner_door', 'seat_boarding'):
        for progress in (False, True, None, '0', [], float('nan'),
                         float('inf'), -float('inf'), -.001, 1.001):
            refused(controller, lambda channel=channel, progress=progress:
                    controller.apply(channel, progress))
    refused(controller, lambda: controller.apply('unknown', .5))
    for channel in ('roof_transfer', 'inner_door', 'seat_boarding'):
        controller.apply(channel, 0)
        assert all(value.is_identity for value in controller.deltas().values())
        controller.apply(channel, 1)
    controller.reset()
    assert controller.derivative_corrections == correction
    assert hashlib.sha256(args.source.read_bytes()).hexdigest() == digest
    print('WAYFARER_OPERATING_INPUT_TEST_PASS: rejected inputs preserve pose; source unchanged')


if __name__ == '__main__':
    main()
