from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class LayoutTest(unittest.TestCase):
    def test_role_libraries_keep_sketches_outside_compiled_src(self):
        for role in ('common', 'hat', 'roomba'):
            library = ROOT / 'roles' / role
            self.assertTrue((library / 'library.properties').is_file(), role)
            self.assertTrue((library / 'src').is_dir(), role)
            self.assertFalse(list((library / 'src').rglob('*.ino')), role)
        for role, sketch in (('common', 'IRProximityDiagnostic'), ('common', 'TailGateDetector'), ('roomba', 'MotorController'), ('roomba', 'RobotReference')):
            self.assertTrue((ROOT / 'roles' / role / 'examples' / sketch / (sketch + '.ino')).is_file(), sketch)

if __name__ == '__main__':
    unittest.main()
