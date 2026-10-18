import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VECTORS = json.loads((ROOT / 'tests/fixtures/meshtastic-v2.json').read_text())


class MeshtasticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='pos-mesh-')
        cls.runner = Path(cls.temp.name) / 'mesh'
        command = ['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I', str(ROOT / 'components/pos_core/include'),
                        '-I', str(ROOT / 'components/pos_install/include'),
                        str(ROOT / 'tests/mesh_runner.c'),
                        str(ROOT / 'components/pos_core/pos_mesh.c')]
        if getattr(cls, 'mbedtls', False):
            build = Path(os.environ['POS_MESH_MBEDTLS_HOST'])
            sdk = Path(os.environ.get('IDF_PATH', '/workspace/pos-cloud/esp-idf-v5.5.5'))
            command += ['-DESP_PLATFORM', '-I', str(ROOT / 'tests/stubs'),
                        '-I', str(sdk / 'components/mbedtls/mbedtls/include'),
                        str(build / 'library/libmbedcrypto.a')]
        else:
            command += ['-lcrypto']
        subprocess.run(command + ['-o', str(cls.runner)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_mesh(self, *args, success=True):
        result = subprocess.run([self.runner, *map(str, args)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result.stdout.strip()

    def test_public_keys_match_independent_x25519_implementation(self):
        for who in ('a', 'b'):
            self.assertEqual(self.run_mesh('public', VECTORS['private_' + who]), VECTORS['public_' + who])

    def test_channel_packets_match_official_protobuf_and_independent_aes_vectors(self):
        for v in VECTORS['vectors']:
            if v['pki']:
                continue
            with self.subTest(v=v['name']):
                body = bytes.fromhex(v['wire'])[16:].hex()
                plain = self.run_mesh('ctr', VECTORS['key'], VECTORS['from_node'], VECTORS['packet_id'], body)
                self.assertEqual(plain, v['data'])
                result = self.run_mesh('decode', VECTORS['private_b'], VECTORS['public_a'], VECTORS['to_node'], v['wire'])
                self.assertEqual(result.splitlines()[-1], v['payload'])

    def test_pki_packets_match_independent_sha256_x25519_ccm_both_directions(self):
        for v in VECTORS['vectors']:
            if not v['pki']:
                continue
            with self.subTest(v=v['name']):
                cipher = self.run_mesh('pki', 'encrypt', VECTORS['private_a'], VECTORS['public_b'], VECTORS['from_node'], VECTORS['packet_id'], VECTORS['extra_nonce'], v['data'])
                self.assertEqual(cipher, bytes.fromhex(v['wire'])[16:].hex())
                plain = self.run_mesh('pki', 'decrypt', VECTORS['private_b'], VECTORS['public_a'], VECTORS['from_node'], VECTORS['packet_id'], 0, cipher)
                self.assertEqual(plain, v['data'])
                result = self.run_mesh('decode', VECTORS['private_b'], VECTORS['public_a'], VECTORS['to_node'], v['wire'])
                self.assertIn('1 0 1', result)

    def test_pki_authentication_rejects_modified_tag_ciphertext_nonce_sender_and_id(self):
        v = next(v for v in VECTORS['vectors'] if v['pki'])
        body = bytearray.fromhex(v['wire'])[16:]
        for at in (0, len(body)-5, len(body)-1):
            changed = body.copy()
            changed[at] ^= 1
            self.run_mesh('pki', 'decrypt', VECTORS['private_b'], VECTORS['public_a'], VECTORS['from_node'], VECTORS['packet_id'], 0, changed.hex(), success=False)
        for sender, ident in ((VECTORS['from_node']+1, VECTORS['packet_id']), (VECTORS['from_node'], VECTORS['packet_id']+1)):
            self.run_mesh('pki', 'decrypt', VECTORS['private_b'], VECTORS['public_a'], sender, ident, 0, body.hex(), success=False)
        self.run_mesh('pki', 'encrypt', VECTORS['private_a'], '00'*32, 12, 15, 0, '0801', success=False)

    def test_default_us_longfast_frequency_and_channel_hash(self):
        self.assertEqual(self.run_mesh('modem', 1, 0, 0), '906875000 250000 11 5 0 8')

    def test_imports_official_channel_url_without_rf_overrides(self):
        self.assertEqual(self.run_mesh('import', VECTORS['channel_url']),
                         '1 0 3 LongFast Private\n' + bytes(range(32)).hex())
        self.run_mesh('import', VECTORS['unsupported_url'], success=False)
        for bad in ('https://example.com/#AQ', 'https://meshtastic.org/e/#A',
                    'https://meshtastic.org/e/#====', 'https://meshtastic.org/e/#AB'):
            self.run_mesh('import', bad, success=False)

    def test_key_input_accepts_official_base64_and_hex_and_rejects_malformed(self):
        import base64
        for key in (b'\1', bytes(range(16)), bytes(range(32))):
            self.assertEqual(self.run_mesh('key', base64.b64encode(key).decode()), key.hex())
            if len(key) != 1:
                self.assertEqual(self.run_mesh('key', key.hex()), key.hex())
        for bad in ('AB==', 'A', 'AQ===', 'AQ==oops', 'AQ ===', ''):
            self.run_mesh('key', bad, success=False)

    def test_rolling_airtime_budget_allows_acks_but_never_expires_airtime_early(self):
        self.run_mesh('airtime')

    def test_eu868_preset_bandwidth_must_fit_region(self):
        self.assertEqual(self.run_mesh('modem', 3, 0, 0).split()[0], '869525000')
        self.run_mesh('modem', 3, 8, 0, success=False)
        self.run_mesh('modem', 1, 2, 0, success=False)
        self.run_mesh('modem', 1, 0, 105, success=False)

    def test_multihop_encrypted_dm_ack_and_duplicate_ack_recovery(self):
        self.run_mesh('engine', 'mesh')

    def test_storage_failure_prevents_ack_and_allows_retry(self):
        self.run_mesh('engine', 'storage')

    def test_bounded_retries_and_timeout(self):
        self.run_mesh('engine', 'retry')

    def test_wrong_peer_ack_unknown_and_changed_keys_never_confirm_delivery(self):
        self.run_mesh('engine', 'ownership')

    def test_duplicate_relay_cancellation_and_hop_limit(self):
        self.run_mesh('engine', 'flood')

    def test_node_discovery_and_peer_gps(self):
        self.run_mesh('engine', 'discovery')

    def test_queue_bounds_malformed_protobuf_and_wire_fuzz(self):
        self.run_mesh('engine', 'bounds')


@unittest.skipUnless(os.environ.get('POS_MESH_MBEDTLS_HOST'), 'Set POS_MESH_MBEDTLS_HOST to the native pinned SDK Mbed TLS build; CI requires this backend')
class MeshtasticMbedTlsTests(MeshtasticTests):
    """Run the same independent vectors through the actual firmware crypto path."""
    mbedtls = True
