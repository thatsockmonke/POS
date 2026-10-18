from pathlib import Path
import asyncio
import importlib.util
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pos_bluetooth', ROOT/'tools/pos_transfer.py')
ble = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ble)


class TransferTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root/'note.txt'
        self.source.write_bytes(bytes(range(256))*19)
        self.receiver = ble.Receiver(self.root/'received')
        self.addCleanup(self.receiver.close)
        self.wire = ble.offer_for(self.source)

    def offer(self):
        for seq, start in enumerate(range(0, 158, 18)):
            self.receiver.feed(b'O'+bytes([seq])+self.wire[start:start+18])
        self.assertEqual(self.receiver.reply[0:1], b'P')

    def data(self):
        data = self.source.read_bytes()
        while self.receiver.offset < len(data):
            at = self.receiver.offset
            frame = b'D'+struct.pack('<I', at)+data[at:at+128]
            self.receiver.feed(frame)
            # Lost ACK retransmission must never duplicate bytes.
            self.receiver.feed(frame)

    def test_wire_format_matches_documented_c_offsets_and_id(self):
        self.assertEqual(len(self.wire), 158)
        ident, size, name, sha = ble.decode(self.wire)
        self.assertEqual(self.wire[13:22], b'note.txt\0')
        self.assertEqual(self.wire[94:], sha.encode())
        self.assertEqual(size, self.source.stat().st_size)
        self.assertEqual(ident, int(sha[:16], 16)^ble.zlib.crc32(b'note.txt'))

    def test_receive_resumes_and_verifies_without_changing_source(self):
        self.offer()
        self.receiver.accept()
        self.receiver.feed(b'D'+struct.pack('<I', 0)+self.source.read_bytes()[:15])
        self.receiver.close()
        self.offer()
        self.receiver.accept()
        self.assertEqual(self.receiver.offset, 15)
        self.data()
        self.assertEqual(self.receiver.feed(b'F'), 'finish')
        self.receiver.finish()
        self.assertEqual(self.receiver.completed.read_bytes(), self.source.read_bytes())
        self.assertFalse(self.receiver.part.exists())
        # Lost final acknowledgment: reoffer verifies existing destination.
        self.offer()
        self.receiver.accept()
        self.assertEqual(self.receiver.reply[:1], b'Z')

    def test_rejects_traversal_reserved_names_and_malformed_offers(self):
        for name in ('../escape', '/tmp/x', '.hidden', 'CON.txt', 'COM1', 'x/y', 'x\\y', 'a:', 'end.', 'end ', 'a'*81):
            with self.subTest(name=name), self.assertRaises(ValueError):
                ble.filename(name)
        for wire in (self.wire[:-1], b'\2'+self.wire[1:], self.wire[:94]+b'g'*64):
            with self.assertRaises(ValueError):
                ble.decode(wire)
        with self.assertRaises(ValueError):
            self.receiver.feed(b'O\1abc')

    def test_cancel_does_not_allow_pending_offer_to_be_accepted_later(self):
        self.offer()
        self.receiver.feed(b'C')
        self.assertIsNone(self.receiver.pending)
        self.assertFalse(list(self.receiver.folder.iterdir()))

    def test_existing_different_file_and_symlinks_are_never_overwritten(self):
        self.offer()
        target = self.receiver.folder/self.source.name
        target.write_bytes(b'precious')
        with self.assertRaises(ValueError):
            self.receiver.accept()
        self.assertEqual(target.read_bytes(), b'precious')
        target.unlink()
        target.symlink_to(self.source)
        with self.assertRaises(OSError):
            self.receiver.accept()
        self.assertEqual(len(self.source.read_bytes()), 4864)

    def test_partial_symlink_and_mismatching_metadata_are_refused(self):
        self.offer()
        ident = ble.decode(self.wire)[0]
        part = self.receiver.folder/f'.pos-share-{ident:016x}.part'
        meta = Path(str(part)+'.offer')
        part.symlink_to(self.source)
        with self.assertRaises(OSError):
            self.receiver.accept()
        self.assertEqual(len(self.source.read_bytes()), 4864)
        part.unlink()
        meta.write_bytes(b'wrong')
        with self.assertRaises(ValueError):
            self.receiver.accept()

    def test_bad_hash_out_of_order_and_oversize_keep_partial(self):
        self.offer()
        self.receiver.accept()
        with self.assertRaises(ValueError):
            self.receiver.feed(b'D'+struct.pack('<I', 3)+b'bad')
        self.data()
        with self.assertRaises(ValueError):
            self.receiver.feed(b'D'+struct.pack('<I', self.receiver.size)+b'x')
        self.receiver.file.seek(0)
        self.receiver.file.write(b'X')
        self.receiver.file.flush()
        with self.assertRaises(ValueError):
            self.receiver.finish()
        self.assertTrue(self.receiver.part.exists())
        self.assertFalse(self.receiver.destination.exists())

    def test_publication_race_never_overwrites_target(self):
        self.offer()
        self.receiver.accept()
        self.data()
        self.receiver.destination.write_bytes(b'new file')
        with self.assertRaises(FileExistsError):
            self.receiver.finish()
        self.assertEqual(self.receiver.destination.read_bytes(), b'new file')
        self.assertTrue(self.receiver.part.exists())

    def test_linux_sender_drives_real_receiver_with_default_mtu(self):
        receiver = self.receiver
        class Client:
            async def write_gatt_char(client, characteristic, frame, response):
                self.assertEqual(characteristic, ble.RX)
                self.assertTrue(response)
                self.assertLessEqual(len(frame), 20)
                result = receiver.feed(frame)
                if receiver.pending and receiver.reply[:1] == b'P':
                    receiver.accept()
                if result == 'finish':
                    receiver.finish()
            async def read_gatt_char(client, characteristic):
                self.assertEqual(characteristic, ble.TX)
                return receiver.reply
        asyncio.run(ble.send_file(Client(), self.source, lambda message: None))
        self.assertEqual(receiver.completed.read_bytes(), self.source.read_bytes())

    def test_empty_file_is_transferred_and_verified(self):
        self.source.write_bytes(b'')
        self.wire = ble.offer_for(self.source)
        self.offer()
        self.receiver.accept()
        self.receiver.finish()
        self.assertEqual(self.receiver.completed.read_bytes(), b'')


class WifiTransferTests(unittest.TestCase):
    def test_connect_timeout_explains_reachability_before_key_check(self):
        from unittest.mock import patch
        with patch.object(ble.http.client.HTTPConnection, 'connect', side_effect=TimeoutError()):
            with self.assertRaisesRegex(ValueError, 'session key has not been checked') as result:
                self.transfer('receive')
        self.assertIn('guest Wi-Fi/client isolation', str(result.exception))
        self.assertNotIn('0123456789abcdef', str(result.exception))
        self.assertFalse(self.folder.exists())

    def test_status_timeout_is_distinguished_from_connect_timeout(self):
        from unittest.mock import patch
        with patch.object(ble.http.client.HTTPConnection, 'getresponse', side_effect=TimeoutError()):
            with self.assertRaisesRegex(ValueError, 'Connected to.*POS did not answer'):
                self.transfer('receive')
        self.assertFalse(self.folder.exists())

    def setUp(self):
        import http.server
        import threading
        import hashlib
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = b'Wi-Fi audio and files\0'*10000
        self.sha = hashlib.sha256(self.data).hexdigest()
        self.upload = None
        self.mode = 'normal'
        self.first_block = threading.Event()
        self.first_progress_before_eof = False
        test = self
        class Server(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args): pass
            def do_POST(handler):
                if handler.headers.get('Authorization') != 'Bearer 0123456789abcdef':
                    handler.send_error(403)
                    return
                test.upload = handler.rfile.read(int(handler.headers['Content-Length']))
                test.assertEqual(handler.path, '/upload?name=music.wav')
                test.assertEqual(hashlib.sha256(test.upload).hexdigest(), handler.headers['X-POS-SHA256'])
                handler.send_response(200)
                handler.end_headers()
                handler.wfile.write(b'Uploaded and verified in Downloads.')
            def do_GET(handler):
                if handler.headers.get('Authorization') != 'Bearer 0123456789abcdef':
                    handler.send_error(403)
                    return
                if handler.path == '/status':
                    handler.send_response(200)
                    handler.end_headers()
                    handler.wfile.write(b'{"format":1,"model":"m5stack-cardputer-adv","version":"test"}')
                    return
                test.assertEqual(handler.path, '/download')
                handler.send_response(302 if test.mode == 'redirect' else 200)
                handler.send_header('Location', 'http://example.com/')
                handler.send_header('X-POS-Filename', '../escape' if test.mode == 'badname' else 'music.wav')
                handler.send_header('X-POS-SHA256', '0'*64 if test.mode == 'hash' else test.sha)
                handler.send_header('Content-Length', str(len(test.data)+(100 if test.mode == 'short' else 0)))
                handler.end_headers()
                if test.mode == 'stream':
                    handler.wfile.write(test.data[:1024]);handler.wfile.flush()
                    test.first_progress_before_eof = test.first_block.wait(2)
                    handler.wfile.write(test.data[1024:])
                else:
                    handler.wfile.write(test.data)
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Server)
        thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        self.address = f'127.0.0.1:{self.server.server_port}'
        self.folder = self.root/'received'

    def transfer(self, action, key='0123456789abcdef'):
        ble.wifi_transfer(action, self.root/'music.wav', self.folder,
                          self.address, key, progress=lambda message: None)

    def test_wifi_streamed_upload_and_verified_download(self):
        (self.root/'music.wav').write_bytes(self.data)
        self.transfer('send')
        self.assertEqual(self.upload, self.data)
        self.transfer('receive')
        self.assertEqual((self.folder/'music.wav').read_bytes(), self.data)
        self.assertFalse(list(self.folder.glob('*.part')))

    def test_wifi_reports_first_data_before_large_or_complete_response(self):
        self.mode='stream'
        messages=[]
        def progress(message):
            messages.append(message)
            if message.startswith('Received:'):self.first_block.set()
        ble.wifi_transfer('receive',None,self.folder,self.address,'0123456789abcdef',progress=progress)
        self.assertTrue(self.first_progress_before_eof)
        self.assertTrue(any('Checking Cardputer' in m for m in messages))
        self.assertTrue(any('preparing' in m for m in messages))
        self.assertEqual((self.folder/'music.wav').read_bytes(),self.data)

    def test_wifi_never_follows_redirect_or_sends_key_to_redirect_destination(self):
        self.mode = 'redirect'
        with self.assertRaisesRegex(ValueError, 'HTTP 302'):
            self.transfer('receive')
        self.assertFalse(self.folder.exists())

    def test_wifi_hash_error_and_truncation_keep_unpublished_partials(self):
        for mode in ('hash', 'short'):
            self.mode = mode
            with self.assertRaises(ValueError):
                self.transfer('receive')
            self.assertFalse((self.folder/'music.wav').exists())
        self.assertEqual(len(list(self.folder.glob('.pos-wifi-*.part'))), 2)

    def test_wifi_bad_key_and_unsafe_name_are_refused(self):
        with self.assertRaisesRegex(ValueError, 'HTTP 403'):
            self.transfer('receive', '0000000000000000')
        self.mode = 'badname'
        with self.assertRaises(ValueError):
            self.transfer('receive')
        self.assertFalse((self.root/'escape').exists())

    def test_wifi_preserves_existing_completed_destination(self):
        self.folder.mkdir()
        target = self.folder/'music.wav'
        target.write_bytes(b'precious')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.transfer('receive')
        self.assertEqual(target.read_bytes(), b'precious')

    def test_wifi_rejects_remote_addresses_urls_with_keys_and_credentials(self):
        for address in ('8.8.8.8', 'https://192.168.1.2', 'http://user:pass@192.168.1.2',
                        'http://192.168.1.2/?key=secret', 'http://192.168.1.2/download', 'example.com'):
            with self.subTest(address=address), self.assertRaises(ValueError):
                ble.wifi_address(address)
        self.assertEqual(ble.wifi_address('192.168.1.2'), ('192.168.1.2', 8080))


class SimpleMenuTests(unittest.TestCase):
    def test_wifi_menu_does_not_need_bluetooth_packages_or_helpers(self):
        from unittest.mock import patch
        import sys
        with patch.object(sys,'argv',['pos-transfer']), \
             patch('builtins.input',side_effect=['1','/tmp/note.txt','1','192.168.1.2','0123456789abcdef']), \
             patch.object(ble,'wifi_transfer') as transfer, \
             patch.object(ble.subprocess,'run') as helper:
            ble.main()
        self.assertEqual(transfer.call_args.args[:2],('send',Path('/tmp/note.txt')))
        self.assertEqual(transfer.call_args.args[3:5],('192.168.1.2','0123456789abcdef'))
        helper.assert_not_called()

    def test_missing_bluez_explains_crostini_and_offers_wifi_without_retrying_bt(self):
        from unittest.mock import patch,AsyncMock
        import sys
        import types
        with patch.object(sys,'argv',['pos-transfer','receive','--bluetooth']), \
             patch.dict(sys.modules,{'bleak':types.ModuleType('bleak'),'dbus_fast':types.ModuleType('dbus_fast')}), \
             patch.object(ble,'linux_session',new=AsyncMock(side_effect=RuntimeError('The name org.bluez was not provided by any .service files'))), \
             patch('builtins.input',side_effect=['y','192.168.1.2','0123456789abcdef']), \
             patch.object(ble,'wifi_transfer') as transfer:
            ble.main()
        self.assertEqual(transfer.call_args.args[0],'receive')
        self.assertIn('Crostini',ble.bluetooth_error(RuntimeError('org.bluez missing')))

    def test_missing_source_file_is_not_misdiagnosed_as_missing_bluez(self):
        self.assertIsNone(ble.bluetooth_error(FileNotFoundError(2,'No such file or directory','/tmp/missing-note.txt')))
