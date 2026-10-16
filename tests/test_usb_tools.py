"""USB orchestration is mocked: these tests never access a real serial port."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from test_installer import posctl

PROBE={"format":1,"chip":"esp32s3","device_id":"aa:bb:cc:dd:ee:ff","plain_development_flash":True,"flash_bytes":4*1024*1024,"observations":{}}

class UsbToolsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="pos-usb-test-");self.addCleanup(self.temp.cleanup)
        self.backup=Path(self.temp.name)/"factory.bin"
        self.data=b"\xe9"+bytes(4*1024*1024-1)

    def backup_device(self,differ=False):
        calls=[]
        def command(port,*args):
            calls.append(args)
            if "read_flash" in args:
                data=self.data if len(calls)==1 or not differ else self.data[:-1]+b"\x01"
                Path(args[-1]).write_bytes(data)
            return "ok"
        with patch.object(posctl,"hardware_probe",return_value=PROBE),patch.object(posctl,"hardware_command",side_effect=command):
            posctl.factory_backup("mock-port",self.backup)
        return calls

    def test_factory_backup_requires_two_matching_reads_and_is_private(self):
        calls=self.backup_device();self.assertEqual([x[2] for x in calls],["read_flash","read_flash"])
        self.assertEqual(self.backup.read_bytes(),self.data);self.assertEqual(self.backup.stat().st_mode&0o777,0o600)
        metadata=json.loads(Path(str(self.backup)+".json").read_text())
        self.assertTrue(metadata["readback_verified"]);self.assertFalse(metadata["recovery_restore_verified"])

    def test_disagreeing_factory_reads_remain_partial(self):
        with self.assertRaises(ValueError):self.backup_device(differ=True)
        self.assertFalse(self.backup.exists());self.assertTrue(Path(str(self.backup)+".part").exists())

    def test_short_stub_packet_restarts_from_zero_in_rom_and_verifies_two_complete_reads(self):
        calls=[]
        def command(port,*args):
            calls.append(args);destination=Path(args[-1])
            self.assertEqual(destination.stat().st_size,0)
            self.assertEqual(destination.stat().st_mode&0o777,0o600)
            self.assertIn("no_reset",args)
            if len(calls)==1:
                destination.write_bytes(b"untrusted short transfer")
                raise posctl.USBCommandError("Corrupt data, expected 0x1000 bytes but received 0xdac bytes")
            self.assertEqual(args[:3],("--no-stub","--after","no_reset"))
            self.assertEqual(args[3:6],("read_flash","0",str(len(self.data))))
            destination.write_bytes(self.data)
            return "ok"
        with patch.object(posctl,"hardware_probe",return_value=PROBE),patch.object(posctl,"hardware_command",side_effect=command):
            posctl.factory_backup("mock-port",self.backup)
        self.assertEqual(len(calls),3)
        self.assertEqual(self.backup.read_bytes(),self.data)
        self.assertEqual(Path(str(self.backup)+".part.stub-failed").read_bytes(),b"untrusted short transfer")
        metadata=json.loads(Path(str(self.backup)+".json").read_text())
        self.assertEqual(metadata["read_methods"],["rom","rom"])
        self.assertTrue(metadata["readback_verified"])

    def test_fallback_is_bounded_and_never_publishes_failed_backup(self):
        with patch.object(posctl,"hardware_probe",return_value=PROBE),patch.object(posctl,"hardware_command",side_effect=posctl.USBCommandError("lost USB")) as command:
            with self.assertRaisesRegex(posctl.USBCommandError,"no firmware was written"):
                posctl.factory_backup("mock-port",self.backup)
        self.assertEqual(command.call_count,2)
        self.assertTrue(all("read_flash" in call.args for call in command.call_args_list))
        self.assertFalse(self.backup.exists());self.assertFalse(Path(str(self.backup)+".json").exists())
        self.assertTrue(Path(str(self.backup)+".part.stub-failed").exists())
        self.assertTrue(Path(str(self.backup)+".part").exists())

    def test_second_pass_failure_still_requires_complete_second_read(self):
        calls=[]
        def command(port,*args):
            calls.append(args)
            if len(calls)==2:raise posctl.USBCommandError("short second transfer")
            Path(args[-1]).write_bytes(self.data if len(calls)==1 else self.data[:-1]+b"\x01")
            return "ok"
        with patch.object(posctl,"hardware_probe",return_value=PROBE),patch.object(posctl,"hardware_command",side_effect=command):
            with self.assertRaisesRegex(ValueError,"do not agree"):posctl.factory_backup("mock-port",self.backup)
        self.assertEqual(len(calls),3);self.assertFalse(self.backup.exists())
        self.assertFalse(Path(str(self.backup)+".json").exists())

    def test_truncated_but_matching_reads_cannot_backup_an_eight_megabyte_device(self):
        with patch.object(posctl,"hardware_probe",return_value={**PROBE,"flash_bytes":8*1024*1024}), \
             patch.object(posctl,"hardware_command",side_effect=lambda port,*args:Path(args[-1]).write_bytes(self.data)):
            with self.assertRaisesRegex(ValueError,"full flash capacity"):posctl.factory_backup("mock-port",self.backup)
        self.assertFalse(self.backup.exists());self.assertFalse(Path(str(self.backup)+".json").exists())

    def test_changed_device_before_retry_or_after_read_is_rejected(self):
        other={**PROBE,"device_id":"00:11:22:33:44:55"}
        with patch.object(posctl,"hardware_probe",side_effect=[PROBE,other]), \
             patch.object(posctl,"hardware_command",side_effect=posctl.USBCommandError("short USB packet")) as command:
            with self.assertRaisesRegex(ValueError,"changed"):posctl.factory_backup("mock-port",self.backup)
        self.assertEqual(command.call_count,1);self.assertFalse(self.backup.exists())

    def test_local_file_errors_are_not_retried_as_usb_errors(self):
        with patch.object(posctl,"hardware_probe",return_value=PROBE), \
             patch.object(posctl,"hardware_command",side_effect=OSError("disk full")) as command:
            with self.assertRaisesRegex(OSError,"disk full"):posctl.factory_backup("mock-port",self.backup)
        self.assertEqual(command.call_count,1);self.assertFalse(self.backup.exists())

    def test_probe_parses_real_flash_capacity_and_backup_probe_avoids_application_boot(self):
        def command(port,*args):
            return {"chip_id":"MAC: 9c:cc:01:e0:7d:0c\n","flash_id":"Detected flash size: 8MB\n",
                    "get_security_info":"Secure Boot: Disabled\nFlash Encryption: Disabled\n"}[args[-1]]
        with patch.object(posctl,"hardware_command",side_effect=command) as usb:
            report=posctl.hardware_probe("mock-port",rom=True)
        self.assertEqual(report["flash_bytes"],8*1024*1024)
        self.assertEqual(report["device_id"],"9c:cc:01:e0:7d:0c")
        self.assertTrue(report["plain_development_flash"])
        for call in usb.call_args_list:self.assertEqual(call.args[1:4],("--no-stub","--after","no_reset"))

    def test_unknown_capacity_and_missing_identity_cannot_start_backup(self):
        for changes in ({"flash_bytes":None},{"device_id":None},{"plain_development_flash":False}):
            with self.subTest(changes=changes),patch.object(posctl,"hardware_probe",return_value={**PROBE,**changes}), \
                 patch.object(posctl,"hardware_command") as command:
                with self.assertRaisesRegex(ValueError,"Cannot verify"):posctl.factory_backup("mock-port",self.backup)
                command.assert_not_called()

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
            self.assertEqual([call.args[3] for call in command.call_args_list],["write_flash","verify_flash","read_mac"])
        evidence=list(self.backup.parent.glob("*.restore-verified-*.json"));self.assertEqual(len(evidence),1)
        report=json.loads(evidence[0].read_text());self.assertTrue(report["flash_readback_verified"]);self.assertFalse(report["factory_boot_observed"])
        self.assertEqual(self.backup.read_bytes(),self.data)

    def test_usb_verification_precedes_first_boot_mutation_of_nvs(self):
        self.backup.write_bytes(self.data)
        flash=bytearray();booted=False
        def command(port,*args):
            nonlocal booted
            operation=args[2]
            if operation=="write_flash":flash[:]=Path(args[-1]).read_bytes()
            if operation=="verify_flash":
                if flash!=Path(args[-1]).read_bytes():raise posctl.USBCommandError("digest mismatch")
                self.assertFalse(booted)
            if args[1]=="hard_reset":
                booted=True;flash[0x9000]^=1 # Health/settings writes on first boot.
            return "ok"
        with patch.object(posctl,"hardware_command",side_effect=command):
            posctl.hardware_write_verified("mock-port",self.backup)
        self.assertTrue(booted)
        self.assertNotEqual(flash,self.data)

    def test_write_or_verify_failure_cannot_trigger_boot(self):
        for fail_at in (1,2):
            with self.subTest(fail_at=fail_at),patch.object(posctl,"hardware_command",side_effect=["ok"]*(fail_at-1)+[posctl.USBCommandError("verification or write failed")]) as command:
                with self.assertRaises(posctl.USBCommandError):posctl.hardware_write_verified("mock-port",self.backup)
                self.assertEqual(command.call_count,fail_at)
                self.assertTrue(all(call.args[1:3]==("--after","no_reset") for call in command.call_args_list))

    def test_restart_failure_distinguishes_completed_verification(self):
        with patch.object(posctl,"hardware_command",side_effect=["ok","ok",posctl.USBCommandError("USB disconnected")]):
            with self.assertRaisesRegex(posctl.USBCommandError,"readback verified.*restart failed"):
                posctl.hardware_write_verified("mock-port",self.backup)


if __name__=="__main__":unittest.main()
