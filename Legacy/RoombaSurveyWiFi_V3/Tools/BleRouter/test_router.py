import unittest

from router import Router


class RouterTests(unittest.TestCase):
    def test_targeted_route_excludes_sender_and_reports_offline(self):
        router = Router()
        router.register('IR-F')
        router.register('IR-L')
        router.unregister('IR-L')
        result = router.route_from_slave('IR-F', 'R IR-F,IR-L,DISPLAY ALERT')
        self.assertEqual(result['deliveries'], [])
        self.assertEqual(result['problems'], [('IR-L', 'offline'), ('DISPLAY', 'unknown')])


if __name__ == '__main__':
    unittest.main()
