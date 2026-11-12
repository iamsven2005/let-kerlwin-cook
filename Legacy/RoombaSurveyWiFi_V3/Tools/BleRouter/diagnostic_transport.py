"""Diagnostic-only legacy hub and selective master; no brain integration."""
import argparse
import asyncio

from router import Router, GROUPS

UART_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
UART_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


class DiagnosticTransport:
    def __init__(self, mode, allow_writes=False):
        if mode not in ('hub', 'master'):
            raise ValueError('unknown diagnostic mode')
        self.mode = mode
        self.allow_writes = allow_writes
        self.router = Router()
        self.boards = {}
        self.roles = {}
        self.tasks = set()

    def add_board(self, address, client):
        self.boards[address] = {'client': client, 'role': None}

    def remove_board(self, address):
        info = self.boards.pop(address, None)
        if info and info['role'] and self.roles.get(info['role']) == address:
            del self.roles[info['role']]
            self.router.unregister(info['role'])

    async def _write(self, address, text, identification=False):
        if not identification and not self.allow_writes:
            print('[diagnostic] write blocked (enable --allow-writes explicitly)')
            return False
        if address not in self.boards:
            return False
        data = text.encode('utf-8')
        if not data or len(data) > 20:
            print('[diagnostic] write rejected: payload must be 1-20 UTF-8 bytes')
            return False
        try:
            await self.boards[address]['client'].write_gatt_char(UART_RX, data, response=False)
            return True
        except Exception as exc:
            print(f'[diagnostic] write failed: {exc}')
            return False

    async def identify(self, address):
        return await self._write(address, 'ID?', identification=True)

    async def on_message(self, address, text):
        info = self.boards.get(address)
        if not info:
            return
        if text.startswith('ID ') and text[3:].strip():
            role = text[3:].strip()
            old_role = info['role']
            if old_role and self.roles.get(old_role) == address:
                del self.roles[old_role]
                self.router.unregister(old_role)
            old_address = self.roles.get(role)
            if old_address and old_address != address and old_address in self.boards:
                self.boards[old_address]['role'] = None
            info['role'] = role
            self.roles[role] = address
            self.router.register(role)
            print(f'[diagnostic] {address}: ID {role}')
            return
        print(f'[diagnostic] {info["role"] or address}: {text}')
        if self.mode == 'hub':
            for target in list(self.boards):
                if target != address:
                    await self._write(target, text)
        elif info['role']:
            result = self.router.route_from_slave(info['role'], text)
            for role, payload in result['deliveries']:
                await self._write(self.roles.get(role), payload)
            for role, reason in result['problems']:
                await self._write(address, f'ERR {role} {reason}')

    async def console_send(self, targets, payload):
        if self.mode == 'hub':
            for address in list(self.boards):
                await self._write(address, payload)
        else:
            roles, problems = self.router.route_from_master(targets)
            for role in roles:
                await self._write(self.roles.get(role), payload)
            for role, reason in problems:
                print(f'[diagnostic] {role}: {reason}')

    def notification(self, address):
        def receive(_sender, data):
            task = asyncio.create_task(self.on_message(address, data.decode(errors='replace').strip()))
            self.tasks.add(task)
            task.add_done_callback(self.tasks.discard)
        return receive


def run_cli(mode):
    parser = argparse.ArgumentParser(description=f'Legacy {mode} diagnostic laptop tool; not robot integration')
    parser.add_argument('--connect', action='store_true', help='explicitly enable scanning and connecting')
    parser.add_argument('--allow-writes', action='store_true', help='DANGER: allow arbitrary forwarded/console writes, including motor commands')
    parser.add_argument('--name-prefix', default='BlueNRG', help='advertised device name prefix')
    args = parser.parse_args()
    if not args.connect:
        print('Diagnostic only: no scan or connection without --connect. Use --help for options.')
        return
    try:
        from bleak import BleakScanner, BleakClient
    except ImportError:
        parser.error('BLE mode requires the optional bleak package')

    async def run():
        transport = DiagnosticTransport(mode, args.allow_writes)
        print('WARNING: diagnostic laptop bridge; writes ' + ('ENABLED, motors may move' if args.allow_writes else 'blocked except ID?'))
        async def discover():
            while True:
                devices = await BleakScanner.discover(timeout=5)
                for device in devices:
                    if not device.name or not device.name.startswith(args.name_prefix) or device.address in transport.boards:
                        continue
                    address = device.address
                    client = BleakClient(device, disconnected_callback=lambda _c, a=address: transport.remove_board(a))
                    try:
                        await client.connect(timeout=15)
                        transport.add_board(address, client)
                        await client.start_notify(UART_TX, transport.notification(address))
                        await transport.identify(address)
                    except Exception as exc:
                        print(f'[diagnostic] connection failed: {exc}')
                        transport.remove_board(address)
                        try:
                            await client.disconnect()
                        except Exception:
                            pass
                await asyncio.sleep(10)
        async def console():
            print('Commands: list | groups | send <targets> <payload> (hub ignores targets)')
            while True:
                try:
                    text = (await asyncio.to_thread(input, '> ')).strip()
                except EOFError:
                    return
                if text == 'list':
                    for address, info in transport.boards.items():
                        print(address, info['role'] or '?')
                elif text == 'groups':
                    print(GROUPS)
                elif text.startswith('send '):
                    parts = text.split(' ', 2)
                    if len(parts) == 3 and parts[2]:
                        await transport.console_send(parts[1], parts[2])
                    else:
                        print('usage: send <targets> <payload>')
        discovery = asyncio.create_task(discover())
        try:
            await console()
        finally:
            discovery.cancel()
            await asyncio.gather(discovery, return_exceptions=True)
            for info in list(transport.boards.values()):
                try:
                    await info['client'].disconnect()
                except Exception as exc:
                    print(f'[diagnostic] disconnect failed: {exc}')
            if transport.tasks:
                await asyncio.gather(*list(transport.tasks), return_exceptions=True)
    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        print('\n[diagnostic] stopped')
