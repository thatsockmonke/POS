from pathlib import Path
import os,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class KnownNetworkTests(unittest.TestCase):
    def test_actual_device_credential_storage_roundtrip_nonce_failure_and_corruption(self):
        sdk=Path(os.environ.get('IDF_PATH','/workspace/pos-cloud/esp-idf-v5.5.5'))
        with tempfile.TemporaryDirectory() as temp:
            runner=Path(temp)/'known'
            command=['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all']
            for path in ('tests/stubs','components/pos_device/include','components/pos_core/include','components/pos_install/include'):
                command+=['-I',str(ROOT/path)]
            command+=['-I',str(sdk/'components/json/cJSON'),str(ROOT/'tests/known_networks_runner.c'),str(ROOT/'components/pos_device/pos_known_networks.c'),str(ROOT/'components/pos_core/pos_network.c'),str(ROOT/'components/pos_core/pos_fs.c'),str(ROOT/'components/pos_install/pos_hash.c'),str(sdk/'components/json/cJSON/cJSON.c'),'-lcrypto','-o',str(runner)]
            subprocess.run(command,check=True)
            subprocess.run([runner],check=True,timeout=20)
