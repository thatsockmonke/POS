import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.environ.get('POS_MESH_MBEDTLS_HOST'), 'Native pinned SDK Mbed TLS required; enabled in CI')
class MeshServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='pos-mesh-service-build-')
        cls.runner = Path(cls.temp.name) / 'service'
        sdk = Path(os.environ.get('IDF_PATH', '/workspace/pos-cloud/esp-idf-v5.5.5'))
        build = Path(os.environ['POS_MESH_MBEDTLS_HOST'])
        command = ['gcc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-DESP_PLATFORM', '-O1',
                   '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all']
        for path in ('tests/mesh_stubs', 'tests/stubs', 'components/pos_core/include',
                     'components/pos_device/include', 'components/pos_install/include'):
            command += ['-I', str(ROOT / path)]
        command += ['-I', str(sdk / 'components/mbedtls/mbedtls/include'),
                    '-I', str(sdk / 'components/json/cJSON')]
        sources = ['tests/mesh_service_runner.c', 'components/pos_device/pos_mesh_service.c',
                   'components/pos_core/pos_mesh.c', 'components/pos_core/pos_network.c',
                   'components/pos_core/pos_fs.c', 'components/pos_core/pos_files.c',
                   'components/pos_core/pos_appdata.c', 'components/pos_install/pos_hash.c']
        command += [str(ROOT / p) for p in sources]
        command += [str(sdk / 'components/json/cJSON/cJSON.c'),
                    str(build / 'library/libmbedcrypto.a'), '-o', str(cls.runner)]
        subprocess.run(command, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pos-mesh-service-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.nvs = self.base / 'nvs'
        self.card = self.base / 'card'
        self.nvs.mkdir()
        self.card.mkdir()
        self.run_service('prepare')

    def run_service(self, action):
        return subprocess.run([self.runner, action, self.nvs, self.card],
                              capture_output=True, text=True, check=True, timeout=20).stdout

    def test_mqtt_decryption_generation_text_filter_duplicate_and_notifications(self):
        self.run_service('mqtt')
        log=self.card/'POS/Users/Owner/Radio/meshtastic.jsonl'
        self.assertEqual(log.read_text().count('Hello'),1)

    def test_interrupted_last_journal_line_does_not_consume_next_message(self):
        self.run_service('send')
        log=self.card/'POS/Users/Owner/Radio/meshtastic.jsonl'
        with log.open('ab') as out:out.write(b'{"event":')
        self.run_service('send')
        import json
        records=[]
        for line in log.read_text().splitlines():
            try:records.append(json.loads(line))
            except json.JSONDecodeError:pass
        self.assertEqual(len(records),2)
        self.assertTrue(all(r['text']=='persist this message' for r in records))

    def test_outgoing_text_is_saved_before_transmission(self):
        self.run_service('send')
        log=self.card/'POS/Users/Owner/Radio/meshtastic.jsonl'
        self.assertIn('"event":"queued"',log.read_text())
        self.assertIn('persist this message',log.read_text())

    def test_encrypted_preferences_failed_write_and_reload_preserve_old_settings(self):
        config = self.nvs / 'config.nvs'
        previous = config.read_bytes()
        self.assertNotIn(b'Panputer POS', previous)
        self.run_service('failed-config')
        self.assertEqual(config.read_bytes(), previous)
        self.run_service('read')
        self.run_service('disabled')

    def test_corrupt_preferences_are_preserved_and_mesh_fails_closed(self):
        config = self.nvs / 'config.nvs'
        blob = bytearray(config.read_bytes())
        blob[-1] ^= 1
        config.write_bytes(blob)
        self.run_service('corrupt')
        self.assertEqual(config.read_bytes(), blob)

    def test_packet_id_reservations_never_reuse_nonce_across_process_restart(self):
        self.run_service('send')
        self.run_service('send')
        files = list((self.card / 'POS/Users/Owner/Radio/mesh-outbox').glob('*.mesh'))
        self.assertEqual(len(files), 2)
        ids = []
        for file in files:
            data = file.read_bytes()
            self.assertEqual(data[:4], b'MSO1')
            self.assertNotIn(b'persist this message', data)
            ids.append(struct.unpack_from('<I', data, 17)[0])
        self.assertEqual(abs(ids[1]-ids[0]), 1024)
        self.assertIn(int(self.run_service('retry')), ids)
        self.assertEqual(len(list(files[0].parent.glob('*.mesh'))), 1)

    def test_failed_nonce_reservation_sends_nothing(self):
        self.run_service('reservation-fail')
        self.assertFalse(list((self.card / 'POS/Users/Owner/Radio/mesh-outbox').glob('*.mesh')))

    def test_peer_identity_persistence_changed_key_block_and_confirmed_forget(self):
        self.run_service('receive-node')
        self.run_service('peer-check')
        self.run_service('keychange')
        self.run_service('peer-changed')
        # Forget is an explicit separate user action, not silent replacement.
        # The runner's changed-key check covers preserved TOFU identity.
        self.run_service('receive-node')
        self.run_service('peer-changed')

    def test_forgetting_a_peer_frees_a_slot_and_survives_restart(self):
        self.run_service('receive-node')
        self.run_service('forget')
        self.run_service('peer-absent')
