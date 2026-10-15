"""USB orchestration is mocked: these tests never access a real serial port."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from test_installer import posctl

PROBE={"format":1,"chip":"esp32s3","device_id":"aa:bb:cc:dd:ee:ff","plain_development_flash":True,"observations":{}}

class UsbToolsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="pos-usb-test-");self.addCleanup(self.temp.cleanup)
        self.backup=Path(self.temp.name)/"factory.bin"
        self.data=b"\xe9"+bytes(4*1024*1024-1)

    def backup_device(self,differ=False):
        calls=[]
        def command(port,*args):
            calls.append(args)
            if args[0]=="read_flash":
                data=self.data if len(calls)==1 or not differ else self.data[:-1]+b"\x01"
                Path(args[-1]).write_bytes(data)
            return "ok"
        with patch.object(posctl,"hardware_probe",return_value=PROBE),patch.object(posctl,"hardware_command",side_effect=command):
            posctl.factory_backup("mock-port",self.backup)
        return calls

    def test_factory_backup_requires_two_matching_reads_and_is_private(self):
        calls=self.backup_device();self.assertEqual([x[0] for x in calls],["read_flash","read_flash"])
        self.assertEqual(self.backup.read_bytes(),self.data);self.assertEqual(self.backup.stat().st_mode&0o777,0o600)
        metadata=json.loads(Path(str(self.backup)+".json").read_text())
        self.assertTrue(metadata["readback_verified"]);self.assertFalse(metadata["recovery_restore_verified"])

    def test_disagreeing_factory_reads_remain_partial(self):
        with self.assertRaises(ValueError):self.backup_device(differ=True)
        self.assertFalse(self.backup.exists());self.assertTrue(Path(str(self.backup)+".part").exists())

    def test_restore_requires_confirmation_and_matching_identity_before_write(self):
        self.backup_device()
        with patch.object(posctl,"hardware_command") as command:
            with self.assertRaises(ValueError):posctl.factory_restore("mock-port",self.backup,False)
            command.assert_not_called()
        other={**PROBE,"device_id":"00:11:22:33:44:55"}
        with patch.object(posctl,"hardware_probe",return_value=other),patch.object(posctl,"hardware_command") as command:
            with self.assertRaises(ValueError):posctl.factory_restore("mock-port",self.backup,True)
            command.assert_not_called()

    def test_corrupt_backup_or_protected_device_prevents_write(self):
        self.backup_device();protected={**PROBE,"plain_development_flash":False}
        with patch.object(posctl,"hardware_probe",return_value=protected),patch.object(posctl,"hardware_command") as command:
            with self.assertRaises(ValueError):posctl.factory_restore("mock-port",self.backup,True)
            command.assert_not_called()
        self.backup.write_bytes(b"corrupt")
        with patch.object(posctl,"hardware_command") as command:
            with self.assertRaises(ValueError):posctl.factory_restore("mock-port",self.backup,True)
            command.assert_not_called()

    def test_restore_verifies_flash_and_never_marks_physical_boot_observed(self):
        self.backup_device()
        with patch.object(posctl,"hardware_probe",return_value=PROBE),patch.object(posctl,"hardware_command",return_value="ok") as command:
            posctl.factory_restore("mock-port",self.backup,True)
            self.assertEqual([call.args[1] for call in command.call_args_list],["write_flash","verify_flash"])
        evidence=list(self.backup.parent.glob("*.restore-verified-*.json"));self.assertEqual(len(evidence),1)
        report=json.loads(evidence[0].read_text());self.assertTrue(report["flash_readback_verified"]);self.assertFalse(report["factory_boot_observed"])
        self.assertEqual(self.backup.read_bytes(),self.data)


if __name__=="__main__":unittest.main()
