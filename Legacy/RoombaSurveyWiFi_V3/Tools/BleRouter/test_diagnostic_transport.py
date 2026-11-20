import unittest
from diagnostic_transport import DiagnosticTransport, UART_RX


class FakeClient:
    def __init__(self):
        self.writes = []

    async def write_gatt_char(self, characteristic, data, response=False):
        self.writes.append((characteristic, data, response))


class TransportTests(unittest.IsolatedAsyncioTestCase):
    async def test_default_blocks_all_forwarding_even_to_unidentified_board(self):
        transport = DiagnosticTransport('hub')
        source, target = FakeClient(), FakeClient()
        transport.add_board('a', source)
        transport.add_board('b', target)
        await transport.on_message('a', 'DRIVE')
        await transport.console_send('ALL', 'DRIVE')
        self.assertEqual(target.writes, [])

    async def test_master_routes_only_explicit_envelopes_with_write_opt_in(self):
        transport = DiagnosticTransport('master', allow_writes=True)
        a, b, c = FakeClient(), FakeClient(), FakeClient()
        for address, client, role in [('a', a, 'IR-F'), ('b', b, 'MOTOR'), ('c', c, 'IR-L')]:
            transport.add_board(address, client)
            await transport.on_message(address, 'ID ' + role)
        await transport.on_message('a', 'DONE')
        await transport.on_message('a', 'R MOTOR GO')
        self.assertEqual(b.writes, [(UART_RX, b'GO', False)])
        self.assertEqual(c.writes, [])

    async def test_hub_broadcast_excludes_sender_when_enabled(self):
        transport = DiagnosticTransport('hub', allow_writes=True)
        a, b = FakeClient(), FakeClient()
        transport.add_board('a', a)
        transport.add_board('b', b)
        await transport.on_message('a', 'hello')
        self.assertEqual(a.writes, [])
        self.assertEqual(b.writes, [(UART_RX, b'hello', False)])

    async def test_role_reassignment_and_disconnect_keep_registry_consistent(self):
        t = DiagnosticTransport('master', allow_writes=True)
        a, b = FakeClient(), FakeClient()
        t.add_board('a', a)
        t.add_board('b', b)
        await t.on_message('a', 'ID MOTOR')
        await t.on_message('b', 'ID MOTOR')
        t.remove_board('a')
        await t.console_send('MOTOR', 'GO')
        self.assertEqual(b.writes, [(UART_RX, b'GO', False)])
        t.remove_board('b')
        self.assertEqual(t.router.route_from_master('MOTOR')[1], [('MOTOR', 'offline')])

    async def test_oversize_payload_is_rejected_not_truncated(self):
        t = DiagnosticTransport('hub', allow_writes=True)
        c = FakeClient()
        t.add_board('b', c)
        await t.console_send('ALL', 'x' * 21)
        self.assertEqual(c.writes, [])

    async def test_identification_is_only_default_write(self):
        t = DiagnosticTransport('master')
        c = FakeClient()
        t.add_board('b', c)
        await t.identify('b')
        self.assertEqual(c.writes, [(UART_RX, b'ID?', False)])
