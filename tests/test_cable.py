"""Real PC tty transport against the actual C USB UI/service over Linux PTYs.
No physical USB or ChromeOS pass-through is claimed.
"""
import contextlib
import os
from pathlib import Path
import pty
import struct
import subprocess
import tempfile
import time
import tty
import unittest
from unittest.mock import patch
import test_features
from test_bluetooth import ble as pc


class CableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_features.FeatureTests.setUpClass()
        cls.runner = test_features.FeatureTests.runner

    @classmethod
    def tearDownClass(cls):
        test_features.FeatureTests.tearDownClass()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pos-cable-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.card = self.root/'card'
        self.card.mkdir()
        self.source = self.root/'sample.bin'
        self.source.write_bytes(bytes(range(256))*23)
        self.output = self.root/'received'

    @contextlib.contextmanager
    def device(self, selected='', decline=False):
        master, slave = pty.openpty()
        tty.setraw(slave)
        process = subprocess.Popen([str(self.runner), str(self.card),
                                    'cable-ui-decline' if decline else 'cable-ui', selected],
                                   stdin=master, stdout=master, stderr=subprocess.PIPE)
        try:
            yield os.ttyname(slave), process
        finally:
            if process.poll() is None:
                process.terminate()
            _, err = process.communicate(timeout=5)
            os.close(master)
            os.close(slave)
            self.assertIn(process.returncode, (0, -15), err.decode(errors='replace'))

    def upload(self, source=None):
        with self.device() as (port, process):
            pc.usb_transfer('send', source or self.source, self.output, port, progress=lambda m: None)
            self.assertEqual(process.wait(timeout=5), 0)

    def connect(self, port, action='send'):
        cable = pc.Cable(port)
        self.addCleanup(cable.close)
        self.assertEqual(cable.hello(action), '0.8.3-dev')
        return cable

    def test_both_directions_roundtrip_real_tty_c_engine_and_ui(self):
        self.source.write_bytes(bytes(range(256))*1000)
        self.upload()
        with self.device('Downloads/sample.bin') as (port, process):
            pc.usb_transfer('receive', None, self.output, port, progress=lambda m: None)
            self.assertEqual(process.wait(timeout=5), 0)
        self.assertEqual((self.card/'Downloads/sample.bin').read_bytes(), self.source.read_bytes())
        self.assertEqual((self.output/'sample.bin').read_bytes(), self.source.read_bytes())

    def test_empty_file_and_identical_completed_retry(self):
        self.source.write_bytes(b'')
        self.upload()
        self.upload()
        self.assertEqual((self.card/'Downloads/sample.bin').read_bytes(), b'')
        self.assertFalse(list((self.card/'Downloads').glob('*.part')))
        with self.device('Downloads/sample.bin') as (port, process):
            pc.usb_transfer('receive',None,self.output,port,progress=lambda m:None)
            self.assertEqual(process.wait(timeout=5),0)
        self.assertEqual((self.output/'sample.bin').read_bytes(),b'')

    def test_declined_offer_creates_no_partial(self):
        with self.device(decline=True) as (port, process):
            with self.assertRaisesRegex(ValueError, 'declined'):
                pc.usb_transfer('send', self.source, self.output, port, progress=lambda m: None)
        self.assertFalse((self.card/'Downloads').exists())

    def test_interrupted_upload_resumes_and_verifies(self):
        wire = pc.offer_for(self.source)
        with self.device() as (port, process):
            cable = self.connect(port)
            self.assertEqual(cable.request(b'O'+wire, lambda m: None), b'A'+bytes(4))
            self.assertEqual(cable.request(b'D'+bytes(4)+self.source.read_bytes()[:512], lambda m: None), b'K'+struct.pack('<I', 512))
            cable.request(b'Q', lambda m: None)
            cable.close()
            self.assertEqual(process.wait(timeout=5), 0)
        self.assertFalse((self.card/'Downloads/sample.bin').exists())
        self.upload()
        self.assertEqual((self.card/'Downloads/sample.bin').read_bytes(), self.source.read_bytes())

    def test_failed_hash_keeps_unpublished_partial(self):
        wire = pc.offer_for(self.source)
        with self.device() as (port, process):
            cable = self.connect(port)
            cable.request(b'O'+wire, lambda m: None)
            data = self.source.read_bytes()
            for at in range(0, len(data), 512):
                cable.request(b'D'+struct.pack('<I', at)+b'x'*len(data[at:at+512]), lambda m: None)
            with self.assertRaisesRegex(ValueError, 'Integrity'):
                cable.request(b'F', lambda m: None)
            cable.request(b'Q', lambda m: None)
            cable.close()
            self.assertEqual(process.wait(timeout=5), 0)
        self.assertFalse((self.card/'Downloads/sample.bin').exists())
        self.assertTrue(list((self.card/'Downloads').glob('*.part')))

    def test_existing_different_card_file_is_not_replaced(self):
        target = self.card/'Downloads/sample.bin'
        target.parent.mkdir()
        target.write_bytes(b'keep')
        with self.device() as (port, process):
            with self.assertRaisesRegex(ValueError, 'already exists'):
                pc.usb_transfer('send', self.source, self.output, port, progress=lambda m: None)
        self.assertEqual(target.read_bytes(), b'keep')

    def test_bad_crc_oversize_and_noise_do_not_process_requests(self):
        with self.device() as (port, process):
            cable = self.connect(port)
            bad = bytearray(pc.usb_frame(43, b'O'+pc.offer_for(self.source)))
            bad[-2] = ord('0') if bad[-2] != ord('0') else ord('1')
            os.write(cable.fd, b'console noise\n'+bad+b'\x1ePOS1'+b'0'*1052+b'\n')
            time.sleep(.03)
            self.assertFalse((self.card/'Downloads').exists())
            self.assertEqual(cable.request(b'Q', lambda m: None), b'K'+bytes(4))
            cable.close()
            self.assertEqual(process.wait(timeout=5), 0)

    def test_lost_ack_retry_is_idempotent(self):
        with self.device() as (port, process):
            cable = self.connect(port)
            cable.request(b'O'+pc.offer_for(self.source), lambda m: None)
            frame = pc.usb_frame(cable.sequence+1, b'D'+bytes(4)+self.source.read_bytes()[:512])
            os.write(cable.fd, frame)
            sequence, response = cable.read(time.monotonic()+5)
            self.assertEqual(response, b'K'+struct.pack('<I', 512))
            os.write(cable.fd, frame)
            while True:
                seq, response = cable.read(time.monotonic()+5)
                if seq == sequence and response[:1] != b'V':break
            self.assertEqual(response, b'K'+struct.pack('<I', 512))
            cable.sequence = sequence
            cable.request(b'Q', lambda m: None)
            cable.close()
            self.assertEqual(process.wait(timeout=5), 0)
        self.assertEqual(next((self.card/'Downloads').glob('*.part')).stat().st_size, 512)

    def test_wrong_direction_is_detected_before_pc_writes(self):
        with self.device() as (port, process):
            cable = pc.Cable(port)
            try:
                with patch.object(os, 'write', wraps=os.write) as write:
                    with self.assertRaisesRegex(ValueError, 'Wrong POS USB screen'):
                        cable.hello('receive')
                    write.assert_not_called()
            finally:
                cable.close()

    def test_old_or_absent_firmware_is_never_probed_with_keyboard_bytes(self):
        master, slave = pty.openpty()
        tty.setraw(slave)
        cable = pc.Cable(os.ttyname(slave))
        try:
            with patch.object(pc.time, 'monotonic', side_effect=[0, 16]), patch.object(os, 'write', wraps=os.write) as write:
                with self.assertRaises(TimeoutError):cable.hello('send')
                write.assert_not_called()
        finally:
            cable.close();os.close(master);os.close(slave)

    def test_upload_cannot_choose_a_remote_system_path(self):
        wire = bytearray(pc.offer_for(self.source))
        wire[13:94] = b'../POS/System/file\0'.ljust(81,b'\0')
        with self.device() as (port, process):
            cable = self.connect(port)
            with self.assertRaisesRegex(ValueError, 'Invalid USB'):
                cable.request(b'O'+wire, lambda m: None)
            cable.request(b'Q', lambda m: None)
            cable.close()
            self.assertEqual(process.wait(timeout=5), 0)
        self.assertFalse((self.card/'POS').exists())

    def test_pc_existing_file_is_preserved(self):
        target=self.card/'Documents/sample.bin';target.parent.mkdir()
        target.write_bytes(self.source.read_bytes())
        self.output.mkdir();(self.output/'sample.bin').write_bytes(b'keep')
        with self.device('Documents/sample.bin') as (port, process):
            with self.assertRaises(ValueError):
                pc.usb_transfer('receive',None,self.output,port,progress=lambda m:None)
        self.assertEqual((self.output/'sample.bin').read_bytes(),b'keep')

    def test_cable_open_close_never_changes_modem_control_lines(self):
        import fcntl
        import termios
        master, slave = pty.openpty()
        try:
            with patch.object(fcntl, 'ioctl', wraps=fcntl.ioctl) as ioctl:
                cable = pc.Cable(os.ttyname(slave)); cable.close()
            self.assertEqual([call.args[1] for call in ioctl.call_args_list],
                             [termios.TIOCEXCL, termios.TIOCNXCL])
        finally:
            os.close(master);os.close(slave)

    def test_pc_decoder_resynchronizes_after_logs_bad_crc_and_oversized_lines(self):
        master, slave = pty.openpty();tty.setraw(slave)
        cable = pc.Cable(os.ttyname(slave))
        try:
            bad=bytearray(pc.usb_frame(7,b'K'+bytes(4)));bad[-2]=ord('g')
            hello=b'H\x01Rm5stack-cardputer-adv\0test'
            os.write(master,b'boot log\n'+bad+b'\x1ePOS1'+b'0'*1080+b'\n'+pc.usb_frame(0,hello))
            self.assertEqual(cable.hello('send'),'test')
        finally:
            cable.close();os.close(master);os.close(slave)

    def test_maximum_frame_accepts_usb_serial_jtag_crlf_line_ending(self):
        master,slave=pty.openpty();tty.setraw(slave)
        cable=pc.Cable(os.ttyname(slave))
        try:
            payload=b'D'+bytes(4)+bytes(range(256))*2
            os.write(master,pc.usb_frame(17,payload).replace(b'\n',b'\r\n'))
            self.assertEqual(cable.read(time.monotonic()+.5),(17,payload))
        finally:
            cable.close();os.close(master);os.close(slave)

    def test_interrupted_pc_download_resumes_from_verified_metadata(self):
        target=self.card/'Documents/sample.bin';target.parent.mkdir()
        target.write_bytes(self.source.read_bytes())
        receiver=pc.Receiver(self.output)
        receiver.pending=pc.offer_for(self.source);receiver.accept()
        receiver.feed(b'D'+bytes(4)+self.source.read_bytes()[:512]);receiver.close()
        with self.device('Documents/sample.bin') as (port,process):
            pc.usb_transfer('receive',None,self.output,port,progress=lambda m:None)
            self.assertEqual(process.wait(timeout=5),0)
        self.assertEqual((self.output/'sample.bin').read_bytes(),self.source.read_bytes())

    def test_usb_menu_uses_no_wifi_or_bluetooth_helpers(self):
        import sys
        with patch.object(sys,'argv',['pos-transfer']),patch('builtins.input',side_effect=['1',str(self.source),'3','1']),patch.object(pc,'usb_ports',return_value=['/dev/ttyACM0']),patch.object(pc,'usb_transfer') as transfer:
            pc.main()
        transfer.assert_called_once_with('send',self.source,Path.home()/'Downloads/POS','/dev/ttyACM0')

    def test_source_offer_rejects_symlinks_and_fifos_without_blocking(self):
        link=self.root/'linked.bin';link.symlink_to(self.source)
        fifo=self.root/'pipe';os.mkfifo(fifo)
        with self.assertRaises(OSError):pc.offer_for(link)
        with self.assertRaises(ValueError):pc.offer_for(fifo)
