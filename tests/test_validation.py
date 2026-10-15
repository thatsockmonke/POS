"""Hardware evidence tooling tests; no actual serial device is used."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location("pos_validation", Path(__file__).resolve().parents[1]/"tools/pos_validation.py")
validation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validation)


class ValidationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pos-evidence-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sd = self.root/"card"; self.sd.mkdir()
        self.output = self.root/"report.json"

    def measurement(self, name, phase, heap, build="a"*64, boot="a"*16):
        folder = self.sd/"Diagnostics"; folder.mkdir(exist_ok=True)
        data = {"format":1,"kind":"pos-device-measurement","measured_on_device":True,
                "build_id":build,"boot_id":boot,"phase":phase,"heap":heap,"minimum_heap":500,
                "largest_internal_block":700,"boot_ms":120,"firmware_bytes":900000,"uptime_ms":1000 if phase.endswith("before") else 2000}
        (folder/name).write_text(json.dumps(data))

    def test_no_measurements_never_counts_as_physical_validation(self):
        report = validation.hardware_report(self.sd,None,self.output)
        self.assertFalse(report["physical_validation_complete"])
        self.assertTrue(all(c["result"]=="not_run" for c in report["checks"].values()))

    def test_measurements_have_hashes_and_compare_only_matching_build_and_boot(self):
        self.measurement("1.json","wifi-before",2000)
        self.measurement("2.json","wifi-connected",1000)
        self.measurement("3.json","apps-before",1800,boot="b"*16)
        self.measurement("4.json","apps-after",1700,boot="c"*16)
        report=validation.hardware_report(self.sd,None,self.output)
        self.assertEqual(len(report["comparisons"]),1)
        self.assertEqual(report["comparisons"][0]["heap_change_bytes"],-1000)
        self.assertEqual(len(report["measurements"][0]["sha256"]),64)
        self.assertFalse(report["physical_validation_complete"])

    def test_report_requires_observation_and_existing_evidence_for_pass(self):
        observation=self.root/"observations.json"
        item={"check":"factory_restore_boot","result":"pass","note":"","evidence":[]}
        observation.write_text(json.dumps({"format":1,"observations":[item]}))
        with self.assertRaises(ValueError):validation.hardware_report(self.sd,observation,self.output)
        item.update(note="Observed the original factory app boot after restore",evidence=["factory-boot.txt"])
        (self.root/"factory-boot.txt").write_text("Operator observation on physical board")
        observation.write_text(json.dumps({"format":1,"observations":[item]}))
        report=validation.hardware_report(self.sd,observation,self.output)
        self.assertEqual(report["checks"]["factory_restore_boot"]["result"],"pass")
        self.assertFalse(report["physical_validation_complete"])

    def test_template_is_unrun_and_outputs_are_never_overwritten(self):
        validation.template(self.output)
        before=self.output.read_bytes()
        with self.assertRaises(ValueError):validation.template(self.output)
        self.assertEqual(self.output.read_bytes(),before)
        self.assertTrue(all(x["result"]=="not_run" for x in json.loads(before)["observations"]))

    def test_bad_measurement_or_external_evidence_is_rejected(self):
        self.measurement("bad.json","apps-before",-1)
        with self.assertRaises(ValueError):validation.hardware_report(self.sd,None,self.output)
        (self.sd/"Diagnostics/bad.json").unlink()
        observation=self.root/"observations.json"
        observation.write_text(json.dumps({"format":1,"observations":[{"check":"display","result":"pass","note":"visible","evidence":["../outside"]}]}))
        with self.assertRaises((ValueError,FileNotFoundError)):validation.hardware_report(self.sd,observation,self.output)

    def test_serial_capture_is_private_and_does_not_request_reset(self):
        device=Mock(); device.read.return_value=b"POS_MEASUREMENT {}\n"
        serial=types.SimpleNamespace(Serial=Mock(return_value=device))
        with patch.dict(sys.modules,{"serial":serial}),patch.object(validation.time,"monotonic",side_effect=[0,0.01,1]):
            validation.serial_logs("mock",0.1,self.output)
        self.assertEqual(self.output.read_bytes(),b"POS_MEASUREMENT {}\n")
        self.assertEqual(self.output.stat().st_mode&0o777,0o600)
        self.assertFalse(device.dtr); self.assertFalse(device.rts)
        device.close.assert_called_once()


if __name__=="__main__":unittest.main()
