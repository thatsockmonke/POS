"""Real private D-Bus, simulated BlueZ: exercise exported GATT/agent contracts.
Run with the pinned Bluetooth helper environment. No radio/hardware is simulated
as physical evidence; this checks the Linux service API and wire exchange only.
"""
import asyncio
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from types import SimpleNamespace
from dbus_fast import Variant, Message
from dbus_fast.aio import MessageBus
from dbus_fast.service import ServiceInterface, method

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pos_ble_smoke', ROOT/'tools/pos_transfer.py')
ble = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ble)


async def run(address, folder):
    os.environ['DBUS_SYSTEM_BUS_ADDRESS'] = address
    bluez = await MessageBus(bus_address=address).connect()
    try:
        await asyncio.wait_for(ble.linux_session(SimpleNamespace(action='receive', folder=folder)),5)
    except Exception as error:
        assert 'Crostini' in (ble.bluetooth_error(error) or ''), repr(error)
    else:
        raise AssertionError('Missing BlueZ must fail clearly before transferring')
    await bluez.request_name('org.bluez')
    registered = asyncio.Event()
    owner = None
    unregistered = []

    class AgentManager(ServiceInterface):
        def __init__(self): super().__init__('org.bluez.AgentManager1')
        @method()
        def RegisterAgent(self, path: 'o', capability: 's'):
            assert path == '/org/pos/agent' and capability == 'DisplayYesNo'
        @method()
        def RequestDefaultAgent(self, path: 'o'): pass
        @method()
        def UnregisterAgent(self, path: 'o'): unregistered.append('agent')

    class Manager(ServiceInterface):
        def __init__(self): super().__init__('org.freedesktop.DBus.ObjectManager')
        @method()
        def GetManagedObjects(self) -> 'a{oa{sa{sv}}}':
            return {'/org/bluez/hci0': {'org.bluez.GattManager1': {}, 'org.bluez.LEAdvertisingManager1': {}}}

    class Gatt(ServiceInterface):
        def __init__(self): super().__init__('org.bluez.GattManager1')
        @method()
        def RegisterApplication(self, path: 'o', options: 'a{sv}'):
            assert path == '/org/pos/share'
        @method()
        def UnregisterApplication(self, path: 'o'): unregistered.append('app')

    class Advertise(ServiceInterface):
        def __init__(self): super().__init__('org.bluez.LEAdvertisingManager1')
        @method()
        def RegisterAdvertisement(self, path: 'o', options: 'a{sv}'):
            assert path == '/org/pos/advertisement'
            registered.set()
        @method()
        def UnregisterAdvertisement(self, path: 'o'): unregistered.append('adv')

    def capture(message):
        nonlocal owner
        if message.member == 'RegisterApplication': owner = message.sender
        return False
    bluez.add_message_handler(capture)
    bluez.export('/org/bluez', AgentManager())
    bluez.export('/', Manager())
    bluez.export('/org/bluez/hci0', Gatt())
    bluez.export('/org/bluez/hci0', Advertise())
    async def approve(text): return True
    ble.ask = approve
    task = asyncio.create_task(ble.linux_session(SimpleNamespace(action='receive', folder=folder)))
    try:
        waiter = asyncio.create_task(registered.wait())
        done, _ = await asyncio.wait([task, waiter], timeout=10, return_when=asyncio.FIRST_COMPLETED)
        if task in done: await task
        assert registered.is_set(), 'Tool did not register GATT application'
        async def iface(path, name):
            intro = await bluez.introspect(owner, path)
            return bluez.get_proxy_object(owner, path, intro).get_interface(name)
        app = await iface('/org/pos/share', 'org.freedesktop.DBus.ObjectManager')
        objects = await app.call_get_managed_objects()
        assert len(objects) == 3
        assert objects['/org/pos/share/service0/rx']['org.bluez.GattCharacteristic1']['Flags'].value == ['write', 'secure-write']
        agent = await iface('/org/pos/agent', 'org.bluez.Agent1')
        await agent.call_request_confirmation('/org/bluez/hci0/dev_TEST', 123456)
        advert = await iface('/org/pos/advertisement', 'org.freedesktop.DBus.Properties')
        props = await advert.call_get_all('org.bluez.LEAdvertisement1')
        assert props['ServiceUUIDs'].value == [ble.SERVICE]
        rx = await iface('/org/pos/share/service0/rx', 'org.bluez.GattCharacteristic1')
        tx = await iface('/org/pos/share/service0/tx', 'org.bluez.GattCharacteristic1')
        options = {'device': Variant('o', '/org/bluez/hci0/dev_TEST')}
        async def wait(code, offset):
            for _ in range(200):
                value = bytes(await tx.call_read_value(options))
                if value == code + struct.pack('<I', offset): return
                assert value[:1] != b'E', value
                await asyncio.sleep(.01)
            raise AssertionError('Timed out waiting for ' + repr(code))
        source = folder.parent/'test.txt'
        source.write_bytes(b'Hello from a simulated Cardputer over real D-Bus!')
        wire = ble.offer_for(source)
        async def offer(offset):
            for sequence, at in enumerate(range(0, 158, 18)):
                await rx.call_write_value(b'O'+bytes([sequence])+wire[at:at+18], options)
                await wait(b'M' if at+18 < 158 else b'A', sequence+1 if at+18 < 158 else offset)
        await offer(0)
        data = source.read_bytes()
        await rx.call_write_value(b'D'+struct.pack('<I', 0)+data[:15], options)
        await wait(b'K', 15)
        await bluez.send(Message.new_signal('/org/bluez/hci0/dev_TEST',
            'org.freedesktop.DBus.Properties', 'PropertiesChanged', 'sa{sv}as',
            ['org.bluez.Device1', {'Connected': Variant('b', False)}, []]))
        await wait(b'W', 0)
        await offer(15)
        for at in range(15, len(data), 15):
            chunk = data[at:at+15]
            await rx.call_write_value(b'D'+struct.pack('<I', at)+chunk, options)
            await wait(b'K', at+len(chunk))
        await rx.call_write_value(b'F', options)
        await wait(b'Z', len(data))
        assert (folder/source.name).read_bytes() == data
    finally:
        task.cancel()
        await asyncio.gather(task, return_exceptions=True)
        bluez.disconnect()
    assert set(unregistered) == {'app', 'adv', 'agent'}, unregistered
    print('PASS: missing BlueZ explained; private D-Bus GATT registration, authenticated flags, numeric-comparison agent, file offer/accept/chunks/disconnect/resume/hash/publication and cleanup (no physical BLE).')


if __name__ == '__main__':
    daemon = subprocess.Popen(['dbus-daemon', '--session', '--nofork', '--print-address=1'], stdout=subprocess.PIPE, text=True)
    try:
        address = daemon.stdout.readline().strip()
        with tempfile.TemporaryDirectory() as temp:
            asyncio.run(run(address, Path(temp)/'received'))
    finally:
        daemon.terminate()
        daemon.wait(timeout=5)
