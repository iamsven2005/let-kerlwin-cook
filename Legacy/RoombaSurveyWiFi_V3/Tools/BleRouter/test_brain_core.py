import random
import unittest

from brain_core import Brain


class BrainTests(unittest.TestCase):
    def test_front_wall_edge_turns_away_from_left_wall(self):
        brain = Brain(random.Random(42))
        brain.on_ir_toggle('L')
        wall, action = brain.on_ir_toggle('F')
        self.assertEqual(wall, 1)
        target, command = brain.drain_outbox()[0]
        self.assertEqual(target, 'MOTOR')
        self.assertTrue(command.startswith('M1 TURN '))
        self.assertTrue(command.endswith(' R'))


if __name__ == '__main__':
    unittest.main()
