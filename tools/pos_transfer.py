#!/usr/bin/env python3
"""POS Transfer: simple streamed Bluetooth and Wi-Fi file transfers."""
import argparse
import asyncio
import hashlib
import getpass
import http.client
import ipaddress
import secrets
from urllib.parse import urlsplit, quote, unquote
import os
from pathlib import Path
import re
import struct
import stat
import subprocess
import sys
import zlib

SERVICE = '73f5ae10-5c12-4b83-9d48-bae023504f53'
RX = '73f5ae11-5c12-4b83-9d48-bae023504f53'
TX = '73f5ae12-5c12-4b83-9d48-bae023504f53'
MAX_SIZE = 0x7fffffff


def filename(name):
    raw = name.encode('utf-8')
    stem = name.split('.')[0].upper()
    if (not raw or len(raw) > 80 or name.startswith('.') or name[-1] in ' .'
            or any(ord(c) < 32 or c in '<>:"/\\|?*' for c in name)
            or stem in {'CON', 'PRN', 'AUX', 'NUL'}
            or re.fullmatch(r'(COM|LPT)[1-9]', stem)):
        raise ValueError('Use a simple filename of at most 80 UTF-8 bytes.')
    return raw


def digest(file):
    h = hashlib.sha256()
    while chunk := file.read(65536):
        h.update(chunk)
    return h.hexdigest()


def offer_for(path):
    name = filename(path.name)
    with path.open('rb') as f:
        if not path.is_file():
            raise ValueError('Choose a regular file.')
        size = os.fstat(f.fileno()).st_size
        if size > MAX_SIZE:
            raise ValueError('Files must be smaller than 2 GiB.')
        sha = digest(f)
    ident = int(sha[:16], 16) ^ zlib.crc32(name)
    return struct.pack('<BQI81s64s', 1, ident, size, name, sha.encode())


def decode(wire):
    if len(wire) != 158:
        raise ValueError('Invalid offer length.')
    version, ident, size, raw, sha = struct.unpack('<BQI81s64s', wire)
    if version != 1 or size > MAX_SIZE or b'\0' not in raw:
        raise ValueError('Unsupported file offer.')
    name = raw.split(b'\0', 1)[0].decode('utf-8')
    filename(name)
    sha = sha.decode('ascii')
    if not re.fullmatch('[0-9a-f]{64}', sha):
        raise ValueError('Invalid SHA-256.')
    return ident, size, name, sha


def secure_open(path, flags):
    fd = os.open(path, flags | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
    if not stat.S_ISREG(os.fstat(fd).st_mode):
        os.close(fd)
        raise ValueError('Only regular files may be used for transfers.')
    return fd


class Receiver:
    """Transport independent receiver. No peer input can overwrite a user file."""
    def __init__(self, folder):
        self.folder = folder.resolve()
        self.folder.mkdir(parents=True, exist_ok=True)
        self.wire = bytearray()
        self.sequence = 0
        self.pending = None
        self.file = None
        self.offset = 0
        self.reply = b'W' + bytes(4)
        self.completed = None
        self.error = ''

    def status(self, code, offset=0):
        self.reply = code.encode() + struct.pack('<I', offset)

    def accept(self):
        self.ident, self.size, self.name, self.sha = decode(self.pending)
        self.destination = self.folder / self.name
        prefix = self.folder / f'.pos-share-{self.ident:016x}'
        self.part = Path(str(prefix) + '.part')
        self.meta = Path(str(self.part) + '.offer')
        if self.destination.exists() or self.destination.is_symlink():
            with os.fdopen(secure_open(self.destination, os.O_RDONLY), 'rb') as f:
                if os.fstat(f.fileno()).st_size != self.size or digest(f) != self.sha:
                    raise ValueError('A different file already has that name; rename the source.')
            self.status('Z', self.size)
            self.completed = self.destination
            return
        try:
            fd = secure_open(self.meta, os.O_WRONLY | os.O_CREAT | os.O_EXCL)
        except FileExistsError:
            with os.fdopen(secure_open(self.meta, os.O_RDONLY), 'rb') as f:
                if f.read(159) != self.pending:
                    raise ValueError('Partial file metadata differs; retained for recovery.')
        else:
            with os.fdopen(fd, 'wb') as f:
                f.write(self.pending)
                f.flush()
                os.fsync(f.fileno())
        self.file = os.fdopen(secure_open(self.part, os.O_RDWR | os.O_CREAT), 'r+b')
        self.offset = self.file.seek(0, os.SEEK_END)
        if self.offset > self.size:
            self.close()
            raise ValueError('Partial file exceeds offered size; retained for recovery.')
        self.status('A', self.offset)

    def feed(self, frame):
        if not frame:
            raise ValueError('Empty command.')
        if frame[:1] == b'O' and not self.file:
            if not 3 <= len(frame) <= 20:
                raise ValueError('Invalid offer fragment.')
            if frame[1] == 0:
                self.wire.clear()
                self.sequence = 0
                self.pending = None
                self.completed = None
            if frame[1] != self.sequence or len(self.wire) + len(frame) - 2 > 158:
                raise ValueError('Out of order offer.')
            self.wire.extend(frame[2:])
            self.sequence += 1
            if len(self.wire) == 158:
                decode(self.wire)
                self.pending = bytes(self.wire)
                self.status('P')
            else:
                self.status('M', self.sequence)
        elif frame[:1] == b'D' and self.file and 6 <= len(frame) <= 133:
            at = struct.unpack_from('<I', frame, 1)[0]
            data = frame[5:]
            if at + len(data) > self.size:
                raise ValueError('Chunk exceeds file.')
            if at == self.offset:
                self.file.seek(at)
                if self.file.write(data) != len(data):
                    raise OSError('Short file write.')
                self.file.flush()
                os.fsync(self.file.fileno())
                self.offset += len(data)
            elif at + len(data) <= self.offset:
                self.file.seek(at)
                if self.file.read(len(data)) != data:
                    raise ValueError('Retry data differs.')
            else:
                raise ValueError('Out of order data.')
            self.status('K', self.offset)
        elif frame == b'F' and self.file:
            self.status('V', self.offset)
            return 'finish'
        elif frame == b'C':
            self.close()
            self.pending = None
            self.wire.clear()
            self.status('W')
        else:
            raise ValueError('Unexpected command.')
        return None

    def finish(self):
        if self.offset != self.size:
            raise ValueError('Incomplete transfer; partial retained.')
        self.file.seek(0)
        if digest(self.file) != self.sha:
            raise ValueError('Hash mismatch; partial retained.')
        self.close()
        # Hard-link publication is atomic and refuses to overwrite existing files.
        os.link(self.part, self.destination, follow_symlinks=False)
        fd = os.open(self.folder, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
        self.part.unlink()
        self.meta.unlink()
        self.completed = self.destination
        self.status('Z', self.size)

    def close(self):
        if self.file:
            self.file.close()
            self.file = None


async def read_line(prompt):
    print(prompt, end='', flush=True)
    loop = asyncio.get_running_loop()
    future = loop.create_future()
    def ready():
        if not future.done():
            line = sys.stdin.readline()
            if not line:
                future.set_exception(EOFError('Terminal closed.'))
            else:
                future.set_result(line.strip())
    loop.add_reader(sys.stdin.fileno(), ready)
    try:
        return await future
    finally:
        loop.remove_reader(sys.stdin.fileno())


async def ask(text):
    return (await read_line(text)).lower() == 'y'


async def send_file(client, path, progress=print):
    wire = await asyncio.to_thread(offer_for, path)
    _, size, _, _ = decode(wire)

    async def wait(code, value=None, seconds=300):
        end = asyncio.get_running_loop().time() + seconds
        while asyncio.get_running_loop().time() < end:
            response = bytes(await client.read_gatt_char(TX))
            if len(response) != 5:
                raise ValueError('Invalid receiver response.')
            status, offset = chr(response[0]), struct.unpack_from('<I', response, 1)[0]
            if status == 'E':
                raise ValueError('Cardputer refused the transfer. Check its screen.')
            if status == code and (value is None or offset == value):
                return offset
            if code == 'A' and status == 'Z' and offset == size:
                return size
            await asyncio.sleep(.08)
        raise TimeoutError('Cardputer did not respond. Retry the same file to resume.')

    for seq, start in enumerate(range(0, 158, 18)):
        await client.write_gatt_char(RX, b'O' + bytes([seq]) + wire[start:start+18], response=True)
        if start + 18 < 158:
            await wait('M', seq + 1, seconds=15)
    progress('Accept the file on the Cardputer.')
    offset = await wait('A')
    if offset > size:
        raise ValueError('Invalid resume offset.')
    # MTU 23 compatible even on BlueZ versions which report a stale MTU.
    with path.open('rb') as source:
        source.seek(offset)
        shown = -1
        while offset < size:
            chunk = source.read(min(15, size-offset))
            if not chunk:
                raise ValueError('Source changed or was truncated.')
            frame = b'D' + struct.pack('<I', offset) + chunk
            for attempt in range(3):
                try:
                    await client.write_gatt_char(RX, frame, response=True)
                    await wait('K', offset + len(chunk), seconds=10)
                    break
                except TimeoutError:
                    if attempt == 2:
                        raise
            offset += len(chunk)
            percent = offset * 100 // size
            if percent != shown:
                progress(f'Sending: {percent}%')
                shown = percent
    await client.write_gatt_char(RX, b'F', response=True)
    await wait('Z', size)
    progress('Sent and verified.')


async def linux_session(args):
    from bleak import BleakClient, BleakScanner
    from dbus_fast import BusType, Variant, DBusError
    from dbus_fast.aio import MessageBus
    from dbus_fast.service import ServiceInterface, method, dbus_property
    from dbus_fast.constants import PropertyAccess

    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    tasks = set()
    approvals = set()
    disk_gate = asyncio.Lock()
    prompt_gate = asyncio.Lock()
    receiver = None
    managers = None
    owner = None
    generation = 0
    resetting = False
    registered = []
    done = asyncio.Event()

    async def disk(function, *values):
        # Serialize worker/approval disk work, including through cancellation.
        async with disk_gate:
            task = asyncio.create_task(asyncio.to_thread(function, *values))
            try:
                return await asyncio.shield(task)
            except asyncio.CancelledError:
                await task
                raise

    async def confirm(text):
        async with prompt_gate:
            return await ask(text)

    def background(coro):
        task = asyncio.create_task(coro)
        tasks.add(task)
        task.add_done_callback(tasks.discard)
        return task

    class Agent(ServiceInterface):
        def __init__(self):
            super().__init__('org.bluez.Agent1')

        @method()
        async def RequestConfirmation(self, device: 'o', passkey: 'u'):
            if not await confirm(f'Pairing code {passkey:06d}. Same code on Cardputer? [y/N] '):
                raise DBusError('org.bluez.Error.Rejected', 'Codes not confirmed')

        @method()
        def RequestAuthorization(self, device: 'o'):
            raise DBusError('org.bluez.Error.Rejected', 'Use numeric comparison')

        @method()
        def AuthorizeService(self, device: 'o', uuid: 's'):
            if uuid.lower() != SERVICE:
                raise DBusError('org.bluez.Error.Rejected', 'Only POS sharing is allowed')

        @method()
        def Cancel(self):
            print('\nPairing cancelled.')

        @method()
        def Release(self):
            pass

    async def proxy(path, interface):
        intro = await bus.introspect('org.bluez', path)
        return bus.get_proxy_object('org.bluez', path, intro).get_interface(interface)

    agent_manager = await proxy('/org/bluez', 'org.bluez.AgentManager1')
    agent_path = '/org/pos/agent'
    bus.export(agent_path, Agent())
    try:
        await agent_manager.call_register_agent(agent_path, 'DisplayYesNo')
        await agent_manager.call_request_default_agent(agent_path)
        if args.action == 'send':
            print('On Cardputer: Share → Receive BLE file. Scanning...')
            found = await BleakScanner.discover(timeout=8, return_adv=True)
            peers = [(d, a) for d, a in found.values() if SERVICE in [u.lower() for u in a.service_uuids]]
            if not peers:
                raise ValueError('No POS receiver found. Open Share → Receive BLE file, then retry.')
            for i, (device, advert) in enumerate(peers, 1):
                print(f'{i}. {advert.local_name or device.name or "POS"}')
            n = int(await read_line('Choose Cardputer: ')) - 1
            if not 0 <= n < len(peers):
                raise ValueError('Invalid selection.')
            async with BleakClient(peers[n][0], timeout=90) as client:
                await client.pair()
                await send_file(client, args.file)
            return

        receiver = Receiver(args.folder)
        root = '/org/pos/share'
        service_path = root + '/service0'
        rx_path, tx_path = service_path + '/rx', service_path + '/tx'

        async def accept_offer(wire, peer):
            try:
                _, size, name, _ = decode(wire)
                if await confirm(f'Receive {name!r} ({size:,} bytes)? [y/N] '):
                    if receiver.pending != wire or owner != peer:
                        return
                    await disk(receiver.accept)
                    if receiver.completed:
                        print(f'Already received and verified: {receiver.completed}')
                else:
                    receiver.status('E')
            except (OSError, ValueError) as e:
                receiver.error = str(e)
                receiver.status('E')
                print(f'Transfer stopped: {e}')

        async def finish():
            try:
                await disk(receiver.finish)
                print(f'Received and verified: {receiver.completed}\nCtrl+C to close, or send another file.')
            except (OSError, ValueError) as e:
                receiver.error = str(e)
                receiver.status('E')
                print(f'Transfer stopped: {e}; partial retained.')

        class Characteristic(ServiceInterface):
            def __init__(self, uuid, receive):
                super().__init__('org.bluez.GattCharacteristic1')
                self.uuid, self.receive = uuid, receive

            @dbus_property(access=PropertyAccess.READ)
            def UUID(self) -> 's':
                return self.uuid

            @dbus_property(access=PropertyAccess.READ)
            def Service(self) -> 'o':
                return service_path

            @dbus_property(access=PropertyAccess.READ)
            def Flags(self) -> 'as':
                return ['write', 'secure-write'] if self.receive else ['read', 'secure-read']

            @method()
            def ReadValue(self, options: 'a{sv}') -> 'ay':
                if self.receive:
                    raise DBusError('org.bluez.Error.NotPermitted', 'Write only')
                return receiver.reply

            @method()
            def WriteValue(self, value: 'ay', options: 'a{sv}'):
                nonlocal owner
                if not self.receive:
                    raise DBusError('org.bluez.Error.NotPermitted', 'Read only')
                device = options.get('device')
                device = device.value if device else None
                if resetting:
                    raise DBusError('org.bluez.Error.InProgress', 'Previous session is closing')
                if not device or (owner and owner != device):
                    raise DBusError('org.bluez.Error.NotPermitted', 'Another peer owns this session')
                owner = device
                if queue.full():
                    raise DBusError('org.bluez.Error.InProgress', 'Wait for acknowledgment')
                epoch = generation
                # Keep disk and approval work out of the D-Bus callback.
                async def process():
                    if epoch != generation or owner != device:
                        return
                    try:
                        result = await disk(receiver.feed, bytes(value))
                        if result == 'finish':
                            await finish()
                        elif receiver.pending and receiver.reply[:1] == b'P':
                            approval = background(accept_offer(receiver.pending, owner))
                            approvals.add(approval)
                            approval.add_done_callback(approvals.discard)
                    except (OSError, ValueError, UnicodeError) as e:
                        receiver.status('E', receiver.offset)
                        print(f'Transfer stopped: {e}; partial retained.')
                # Queue writes, including retries, in exact arrival order.
                queue.put_nowait(process)

        class Service(ServiceInterface):
            def __init__(self):
                super().__init__('org.bluez.GattService1')

            @dbus_property(access=PropertyAccess.READ)
            def UUID(self) -> 's':
                return SERVICE

            @dbus_property(access=PropertyAccess.READ)
            def Primary(self) -> 'b':
                return True

        rx, tx, service = Characteristic(RX, True), Characteristic(TX, False), Service()
        objects = {service_path: service, rx_path: rx, tx_path: tx}

        class Application(ServiceInterface):
            def __init__(self):
                super().__init__('org.freedesktop.DBus.ObjectManager')

            @method()
            def GetManagedObjects(self) -> 'a{oa{sa{sv}}}':
                return {service_path: {'org.bluez.GattService1': {
                    'UUID': Variant('s', SERVICE), 'Primary': Variant('b', True)}},
                    **{p: {'org.bluez.GattCharacteristic1': {
                        'UUID': Variant('s', c.uuid), 'Service': Variant('o', service_path),
                        'Flags': Variant('as', c.Flags)}} for p, c in ((rx_path, rx), (tx_path, tx))}}

        class Advertisement(ServiceInterface):
            def __init__(self):
                super().__init__('org.bluez.LEAdvertisement1')

            @dbus_property(access=PropertyAccess.READ)
            def Type(self) -> 's':
                return 'peripheral'

            @dbus_property(access=PropertyAccess.READ)
            def ServiceUUIDs(self) -> 'as':
                return [SERVICE]

            @dbus_property(access=PropertyAccess.READ)
            def LocalName(self) -> 's':
                return 'POS Linux'

            @method()
            def Release(self):
                pass

        manager = await proxy('/', 'org.freedesktop.DBus.ObjectManager')
        managed = await manager.call_get_managed_objects()
        adapters = [p for p, interfaces in managed.items()
                    if 'org.bluez.GattManager1' in interfaces and 'org.bluez.LEAdvertisingManager1' in interfaces]
        if not adapters:
            raise ValueError('No Bluetooth adapter supports receiving/advertising. Use native Linux with a BLE adapter.')
        adapter = adapters[0]
        managers = (await proxy(adapter, 'org.bluez.GattManager1'),
                    await proxy(adapter, 'org.bluez.LEAdvertisingManager1'))
        bus.export(root, Application())
        for path, obj in objects.items():
            bus.export(path, obj)
        adv_path = '/org/pos/advertisement'
        bus.export(adv_path, Advertisement())
        queue = asyncio.Queue(maxsize=8)

        async def worker():
            while True:
                work = await queue.get()
                await work()

        def disconnected(message):
            nonlocal owner, generation, resetting
            if (message.interface == 'org.freedesktop.DBus.Properties'
                    and message.member == 'PropertiesChanged' and message.path == owner
                    and message.body and message.body[0] == 'org.bluez.Device1'):
                connected = message.body[1].get('Connected')
                if connected is not None and not connected.value:
                    owner = None
                    generation += 1
                    resetting = True
                    for approval in list(approvals):
                        approval.cancel()
                    # Queue cleanup behind any in-flight disk operation.
                    async def reset():
                        nonlocal resetting
                        async with disk_gate:
                            receiver.close()
                            receiver.pending = None
                            receiver.wire.clear()
                            receiver.status('W')
                            resetting = False
                    background(queue.put(reset))
            return False

        # Device signals are delivered once BlueZ object manager is subscribed.
        bus.add_message_handler(disconnected)
        from dbus_fast import Message
        await bus.call(Message(destination='org.freedesktop.DBus', path='/org/freedesktop/DBus',
                               interface='org.freedesktop.DBus', member='AddMatch', signature='s',
                               body=["type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"]))
        background(worker())
        await managers[0].call_register_application(root, {})
        registered.append('app')
        await managers[1].call_register_advertisement(adv_path, {})
        registered.append('adv')
        print(f'Receiving into {receiver.folder}\nOn Cardputer: Files → choose file → Share → Send over BLE → POS Linux.\nCompare codes on both devices. Ctrl+C closes; partial files remain for retry.')
        await done.wait()
    finally:
        if managers:
            for kind, manager, path in [('adv', managers[1], '/org/pos/advertisement'), ('app', managers[0], '/org/pos/share')]:
                if kind in registered:
                    try:
                        await (manager.call_unregister_advertisement(path) if kind == 'adv'
                               else manager.call_unregister_application(path))
                    except Exception:
                        pass
        for task in list(tasks):
            task.cancel()
        await asyncio.gather(*list(tasks), return_exceptions=True)
        if receiver:
            receiver.close()
        try:
            await agent_manager.call_unregister_agent(agent_path)
        finally:
            bus.disconnect()


def wifi_address(value):
    parsed = urlsplit(value if '://' in value else 'http://' + value)
    if (parsed.scheme != 'http' or not parsed.hostname or parsed.username
            or parsed.password or parsed.query or parsed.fragment
            or parsed.path not in ('', '/')):
        raise ValueError('Enter only the local Cardputer IP shown on screen (no key or path).')
    address = ipaddress.ip_address(parsed.hostname)
    if not (address.is_private or address.is_link_local or address.is_loopback):
        raise ValueError('Use the Cardputer local-network IP address.')
    return str(address), parsed.port or 8080


def wifi_transfer(action, source, folder, address, key, save_name=None, progress=print):
    host, port = wifi_address(address)
    if not re.fullmatch('[0-9a-fA-F]{16}', key):
        raise ValueError('The session key must have exactly 16 hexadecimal characters.')
    connection = http.client.HTTPConnection(host, port, timeout=120)
    headers = {'Authorization': 'Bearer ' + key.lower()}
    try:
        if action == 'send':
            # Hash and send the same open file; never load it all into RAM.
            filename(source.name)
            with os.fdopen(secure_open(source, os.O_RDONLY), 'rb') as file:
                size = os.fstat(file.fileno()).st_size
                if size > MAX_SIZE:
                    raise ValueError('Files must be smaller than 2 GiB.')
                sha = digest(file)
                file.seek(0)
                connection.putrequest('POST', '/upload?name=' + quote(source.name, safe=''))
                for name, value in {**headers, 'Content-Length': str(size), 'X-POS-SHA256': sha}.items():
                    connection.putheader(name, value)
                connection.endheaders()
                remaining = size
                while remaining:
                    chunk = file.read(min(65536, remaining))
                    if not chunk:
                        raise ValueError('Source changed; inspect the POS partial before retrying.')
                    connection.send(chunk)
                    remaining -= len(chunk)
                    progress(f'Sending: {(size-remaining)*100//size}%')
            response = connection.getresponse()
            if response.status != 200:
                raise ValueError(f'Cardputer returned HTTP {response.status}: ' + response.read(1024).decode(errors='replace'))
            message = response.read(1024).decode(errors='replace')
            if 'verified' not in message.lower():
                raise ValueError('Cardputer did not confirm verification.')
            progress('Sent and verified in Cardputer Downloads.')
            return
        connection.request('GET', '/download', headers=headers)
        response = connection.getresponse()
        if response.status != 200:
            raise ValueError(f'Cardputer returned HTTP {response.status}. Select a file on POS and check the session key.')
        sha = response.getheader('X-POS-SHA256', '')
        if not re.fullmatch('[0-9a-f]{64}', sha):
            raise ValueError('Missing or invalid file hash; transfer refused.')
        name = save_name or unquote(response.getheader('X-POS-Filename', ''), errors='strict')
        if not name:
            name = input('Save as (filename, including extension): ').strip()
        filename(name)
        folder = folder.resolve()
        folder.mkdir(parents=True, exist_ok=True)
        destination = folder / name
        if destination.exists() or destination.is_symlink():
            raise ValueError('Destination already exists; move it or choose a different --name.')
        part = folder / ('.pos-wifi-' + secrets.token_hex(8) + '.part')
        h = hashlib.sha256()
        received = 0
        try:
            with os.fdopen(secure_open(part, os.O_CREAT | os.O_EXCL | os.O_WRONLY), 'wb') as file:
                while chunk := response.read(65536):
                    received += len(chunk)
                    if received > MAX_SIZE:
                        raise ValueError('File exceeds the 2 GiB limit.')
                    file.write(chunk)
                    h.update(chunk)
                    progress(f'Received: {received:,} bytes')
                file.flush()
                os.fsync(file.fileno())
            if response.length not in (None, 0):
                raise ValueError('Download was truncated; partial retained.')
            if h.hexdigest() != sha:
                raise ValueError('SHA-256 mismatch.')
            os.link(part, destination, follow_symlinks=False)
            fd = os.open(folder, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
            part.unlink()
        except BaseException:
            progress(f'Incomplete file retained: {part}')
            raise
        progress(f'Received and verified: {destination}')
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description='POS Transfer — Bluetooth and Wi-Fi files. Ctrl+C cancels.')
    parser.add_argument('action', nargs='?', choices=['send', 'receive'])
    parser.add_argument('file', nargs='?', type=Path)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument('--wifi', dest='transport', action='store_const', const='wifi')
    transport.add_argument('--bluetooth', dest='transport', action='store_const', const='bluetooth')
    parser.add_argument('--address', help='Cardputer local IP (Wi-Fi)')
    parser.add_argument('--name', help='Save incoming Wi-Fi file with this name')
    parser.add_argument('--folder', type=Path, default=Path.home() / 'Downloads' / 'POS')
    args = parser.parse_args()
    if not args.action:
        print('POS Transfer\n1. Send to Cardputer\n2. Receive from Cardputer')
        choice = input('Choose a number (Ctrl+C cancels): ').strip()
        if choice not in ('1', '2'):
            parser.error('Choose 1 or 2.')
        args.action = 'send' if choice == '1' else 'receive'
    if args.action == 'send' and not args.file:
        args.file = Path(input('File to send (paste path): ').strip().strip('"')).expanduser()
    if not args.transport:
        print('1. Wi-Fi (faster)\n2. Bluetooth')
        choice = input('Connection: ').strip()
        if choice not in ('1', '2'):
            parser.error('Choose 1 or 2.')
        args.transport = 'wifi' if choice == '1' else 'bluetooth'
    if args.transport == 'wifi':
        print('Use the same trusted Wi-Fi network. Transfers use local HTTP with a temporary key.')
        print('On POS: Share → Receive Wi-Fi files / PC.' if args.action == 'send' else
              'On POS: Files → choose file → Share → Wi-Fi file transfer.')
        address = args.address or input('Cardputer IP shown on screen: ').strip()
        key = getpass.getpass('Session key shown on Cardputer: ').strip()
        wifi_transfer(args.action, args.file, args.folder, address, key, args.name)
        return
    if sys.platform != 'linux':
        parser.error('Bluetooth needs Linux and BlueZ.')
    try:
        import bleak  # noqa: F401
        import dbus_fast  # noqa: F401
    except ImportError:
        helper = Path.home() / '.local/share/pos-transfer'
        python = helper / 'bin/python'
        if not python.exists():
            print('Bluetooth helpers need a one-time install into your home folder.')
            if input('Install them now? [y/N] ').strip().lower() != 'y':
                raise SystemExit('Cancelled. No files transferred.')
            subprocess.run([sys.executable, '-m', 'venv', str(helper)], check=True)
        ready = subprocess.run([str(python), '-c', 'import bleak, dbus_fast'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not ready:
            subprocess.run([str(python), '-m', 'pip', '--isolated', 'install',
                        '--disable-pip-version-check', 'bleak==0.22.3', 'dbus-fast==2.44.5'], check=True)
        # Keep the chosen action, without a second menu.
        command = [str(python), str(Path(__file__).resolve()), args.action]
        if args.file:
            command.append(str(args.file))
        command.extend(['--bluetooth', '--folder', str(args.folder)])
        os.execv(str(python), command)
    asyncio.run(linux_session(args))


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nClosed. Partial files are kept. Bluetooth supports resume; Wi-Fi needs a fresh transfer.')
    except Exception as error:
        print(f'Transfer stopped: {error}\nSee the transfer guide for Wi-Fi, BlueZ and Crostini connection help.', file=sys.stderr)
        sys.exit(1)
