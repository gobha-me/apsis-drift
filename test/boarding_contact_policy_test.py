#!/usr/bin/env python3
"""Contact exceptions cannot conceal a changed pair or forbidden motion phase."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build_boarding_qualification import classify


class ContactPolicyTest(unittest.TestCase):
    def classify_one(self, a, b, channel, progress, owner='craft'):
        contact = {'object_a': a, 'object_b': b, 'samples': len(progress),
                   'first_progress': progress[0], 'last_progress': progress[-1],
                   'triangle_pairs_max': 1, 'progress_samples': progress}
        raw = {'sweeps': [{'owner': owner, 'channel': channel, 'samples': 41,
                          'method': 'fixture', 'contacts': [contact]}]}
        return classify(raw)[0][0]

    def test_pin_contact_fails_during_free_turn(self):
        row = self.classify_one('Swivel lock pin', 'Swivel bearing race', 'seat_boarding', [.80])
        self.assertEqual(len(row['unexpected_contacts']), 1)

    def test_pin_contact_accepted_only_when_aligned(self):
        row = self.classify_one('Swivel lock pin', 'Swivel bearing race', 'seat_boarding', [.575, .95, 1.])
        self.assertEqual(len(row['intentional_contacts']), 1)

    def test_other_channel_freezes_aligned_seat(self):
        row = self.classify_one('Swivel lock pin', 'Swivel bearing race', 'inner_door', [.80])
        self.assertEqual(len(row['intentional_contacts']), 1)

    def test_similar_unknown_name_is_not_waived(self):
        row = self.classify_one('Swivel lock pin', 'Swivel bearing race.001', 'seat_boarding', [0.])
        self.assertEqual(len(row['unexpected_contacts']), 1)

    def test_link_foot_contact_fails_after_closed_stop(self):
        row = self.classify_one('AFT01 | hatch hinge fixed foot -1 -0.2', 'AFT01 | hatch hinge link -1 -0.2', 'roof_transfer', [.125])
        self.assertEqual(len(row['unexpected_contacts']), 1)

    def test_nonfinite_progress_fails(self):
        row = self.classify_one('Swivel lock pin', 'Swivel bearing race', 'seat_boarding', [float('nan')])
        self.assertEqual(len(row['unexpected_contacts']), 1)


if __name__ == '__main__':
    unittest.main()
