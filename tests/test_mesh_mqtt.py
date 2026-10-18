from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class MeshMqttTests(unittest.TestCase):
 def test_real_transport_event_handling_with_sdk_adapter(self):
  with tempfile.TemporaryDirectory() as tmp:
   binary=Path(tmp)/'mqtt'
   cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all']
   for path in ['tests/mqtt_stubs','tests/mesh_stubs','components/pos_core/include','components/pos_device/include','components/pos_install/include']:
    cmd+=['-I',str(ROOT/path)]
   subprocess.run(cmd+[str(ROOT/'tests/mqtt_runner.c'),str(ROOT/'components/pos_device/pos_mesh_mqtt.c'),'-o',str(binary)],check=True)
   result=subprocess.run([binary],capture_output=True,text=True,timeout=5)
   self.assertEqual(result.returncode,0,result.stdout+result.stderr)
