"""Wizard safety tests. All disk mutations, privilege and USB calls are mocked.
No physical disk, partition, mount or Cardputer is modified by this suite.
"""
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import zipfile
from test_installer import image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import pos_setup as wizard
import pos_setup_storage as storage
import build_setup
import posctl


def disk(**changes):
    value = {"name":"sdb", "path":"/dev/sdb", "type":"disk", "size":64_000_000_000,
             "rm":True,"hotplug":True,"ro":False,"tran":"usb","model":"SD card reader",
             "serial":"reader-123","wwn":"","diskseq":"7","maj:min":"8:16","log-sec":512,
             "mountpoints":[],"children":[{"path":"/dev/sdb1","type":"part","maj:min":"8:17", "ro":False,
              "fstype":"vfat","fsver":"FAT32","mountpoints":["/media/test/CARD"]}]}
    value.update(changes)
    return value


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="pos-setup-test-");self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.state=self.root/"private";self.state.mkdir(mode=0o700)
        self.card=self.root/"card";self.card.mkdir();self.package=self.root/"package";self.package.mkdir()
        (self.package/"manifest.pos").write_text("test manifest")
        (self.package/"firmware.bin").write_bytes(bytes(range(256))*500)
        (self.package/"payload/System").mkdir(parents=True);(self.package/"payload/System/README.txt").write_text("POS")

    def test_yes_no_typo_reprompts_instead_of_declining_factory_boot(self):
        with patch("builtins.input",side_effect=["t","y"]),patch.object(wizard,"say") as say:
            self.assertTrue(wizard.ask("Factory software visible?"))
        self.assertIn("not accepted",say.call_args.args[0])
        with patch("builtins.input",side_effect=["maybe","no"]):self.assertFalse(wizard.ask("Continue?",default=True))
        with patch("builtins.input",return_value=""):self.assertFalse(wizard.ask("Continue?"))

    def test_backup_labels_identify_pos_snapshot_and_cannot_mislabel_it_as_factory(self):
        backup=self.root/"snapshot.bin"
        backup.write_bytes(bytes(0x10000)+image("0.5.3-dev","pos_installer"))
        self.assertEqual(wizard.backup_pos_version(backup),"0.5.3-dev")
        self.assertIn("POS 0.5.3-dev snapshot",wizard.recovery_backup_label(backup))
        backup.write_bytes(bytes(0x10000)+image("1.0.0","stock_demo"))
        self.assertIsNone(wizard.backup_pos_version(backup))
        self.assertIn("identity needs confirmation",wizard.recovery_backup_label(backup))
        metadata=dict(sha256=storage.sha256(backup),device_id="aa:bb:cc:dd:ee:ff")
        Path(str(backup)+".json").write_text(json.dumps(metadata))
        Path(str(backup)+".pos-setup-recovery.json").write_text(json.dumps(dict(factory_boot_observed=True,backup_sha256=metadata["sha256"],device_id=metadata["device_id"])))
        self.assertIn("factory boot previously confirmed",wizard.recovery_backup_label(backup))
        backup.write_bytes(bytes(0x10000)+image("0.5.3-dev","pos_installer"))
        self.assertIn("not factory firmware",wizard.recovery_backup_label(backup))

    def test_selected_pos_snapshot_is_preserved_and_cannot_be_restored_as_factory(self):
        backup=self.root/"old-pos.bin";backup.write_bytes(bytes(0x10000)+image("0.5.3-dev","pos_installer"))
        Path(str(backup)+".json").write_text(json.dumps(dict(device_id="aa:bb:cc:dd:ee:ff")))
        with patch.object(wizard,"serial_port",return_value="mock-port"),patch.object(wizard,"ask",return_value=True), \
             patch.object(wizard,"saved_backups",return_value=[backup]),patch.object(wizard,"choose",return_value=0), \
             patch.object(posctl,"hardware_probe",return_value=dict(device_id="aa:bb:cc:dd:ee:ff",plain_development_flash=True)), \
             patch.object(posctl,"factory_restore") as restored:
            with self.assertRaisesRegex(storage.SetupError,"contains POS 0.5.3-dev"):
                wizard.backup_and_recovery(self.state)
        restored.assert_not_called();self.assertTrue(backup.exists())

    def bundle(self, extra=None, edit_manifest=None):
        firmware=image("0.5.1-dev");payload=b"POS system asset"
        record=lambda p,b:dict(path=p,size=len(b),sha256=hashlib.sha256(b).hexdigest())
        manifest=dict(format=1,model=posctl.MODEL,version="0.5.1-dev",firmware=record("firmware.bin",firmware),files=[record("System/README.txt",payload)])
        if edit_manifest:edit_manifest(manifest)
        files={"card/pos-install/manifest.pos":json.dumps(manifest).encode(),"card/pos-install/firmware.bin":firmware,
               "card/pos-install/payload/System/README.txt":payload,"pos-installer-usb.bin":bytes(0x10000)+image("0.5.1-dev","pos_installer"),
               "tools/posctl.py":b'raise RuntimeError("Downloaded code must not run")',"INSTALL.md":b"instructions"}
        if extra:files.update(extra)
        sums="".join(hashlib.sha256(b).hexdigest()+"  "+p+"\n" for p,b in files.items()).encode()
        files["SHA256SUMS"]=sums
        archive=self.root/"pos-0.5.1-dev.zip"
        with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED) as z:
            for p,b in files.items():z.writestr(p,b)
        Path(str(archive)+".sha256").write_text(storage.sha256(archive)+"  "+archive.name+"\n")
        return archive

    def test_latest_includes_prereleases_skips_drafts_and_compares_backports(self):
        def release(tag,**kw):return dict(tag_name=tag,draft=False,assets=[dict(name="pos-"+tag[1:]+".zip"),dict(name="pos-"+tag[1:]+".zip.sha256")],**kw)
        releases=[release("v0.5.0-dev"),release("v0.5.1-dev",prerelease=True),release("v0.4.9")]
        releases.append({**release("v9.0.0"),"draft":True})
        gh=wizard.GitHub();gh.json=Mock(return_value=releases)
        self.assertEqual(gh.release()["tag_name"],"v0.5.1-dev")
        releases.append(release("v0.5.1"));self.assertEqual(gh.release()["tag_name"],"v0.5.1")

    def test_release_missing_archive_or_checksum_is_not_installable(self):
        gh=wizard.GitHub();gh.json=Mock(return_value=[dict(tag_name="v0.5.1-dev",draft=False,assets=[dict(name="pos-0.5.1-dev.zip")])])
        with self.assertRaises(storage.SetupError):gh.release()

    def test_complete_bundle_is_checked_without_executing_downloaded_tools(self):
        target,manifest=wizard.extract_verified(self.bundle(),self.state)
        self.assertEqual(manifest["version"],"0.5.1-dev")
        self.assertEqual((target/"card/pos-install/payload/System/README.txt").read_bytes(),b"POS system asset")

    def test_bad_download_checksum_stops_before_extraction(self):
        archive=self.bundle();Path(str(archive)+".sha256").write_text("0"*64+"  "+archive.name+"\n")
        with self.assertRaises(storage.SetupError):wizard.extract_verified(archive,self.state)
        self.assertFalse((self.state/"unpacked").exists())

    def test_outer_checksum_does_not_hide_inner_manifest_mismatch(self):
        archive=self.bundle(edit_manifest=lambda m:m["files"][0].update(sha256="0"*64))
        with self.assertRaises(storage.SetupError):wizard.extract_verified(archive,self.state)

    def test_unexpected_card_files_are_refused(self):
        archive=self.bundle(extra={"card/pos-install/unexpected.py":b"unwanted"})
        with self.assertRaises(storage.SetupError):wizard.extract_verified(archive,self.state)

    def test_zip_traversal_symlink_case_and_file_parent_collisions_refused(self):
        for names in (("../escape",),("/absolute",),("a\\b",),("A","a"),("A","a/b")):
            with self.subTest(names=names):
                b=io.BytesIO()
                with zipfile.ZipFile(b,"w") as z:
                    for name in names:z.writestr(name,b"x")
                with zipfile.ZipFile(io.BytesIO(b.getvalue())) as z:
                    with self.assertRaises(storage.SetupError):wizard.members(z)
        b=io.BytesIO();info=zipfile.ZipInfo("link");info.create_system=3;info.external_attr=(stat.S_IFLNK|0o777)<<16
        with zipfile.ZipFile(b,"w") as z:z.writestr(info,"/etc/passwd")
        with zipfile.ZipFile(io.BytesIO(b.getvalue())) as z:
            with self.assertRaises(storage.SetupError):wizard.members(z)

    def test_extraction_size_and_unchecked_files_are_refused(self):
        b=io.BytesIO()
        with zipfile.ZipFile(b,"w") as z:z.writestr("huge",b"x"*100)
        with patch.object(wizard,"MAX_UNPACK",20),zipfile.ZipFile(io.BytesIO(b.getvalue())) as z:
            with self.assertRaises(storage.SetupError):wizard.members(z)
        archive=self.bundle()
        with zipfile.ZipFile(archive,"a") as z:z.writestr("unchecked",b"x")
        Path(str(archive)+".sha256").write_text(storage.sha256(archive)+"  "+archive.name+"\n")
        with self.assertRaises(storage.SetupError):wizard.extract_verified(archive,self.state)

    def test_internal_root_home_swap_mapped_readonly_and_large_disks_excluded(self):
        for d,protected in ((disk(rm=False,hotplug=False,tran="sata"),set()),(disk(),{"8:17"}),
            (disk(ro=True),set()),(disk(size=2**41),set()),(disk(size=None),set()),(disk(**{"log-sec":4096}),set()),(disk(diskseq=None),set())):
            self.assertEqual(storage.candidates(storage.Inventory([d],protected)),[])
        for mount in ("/","/home","/boot/efi","/var","[SWAP]","/mnt/data"):
            d=disk();d["children"][0]["mountpoints"]=[mount]
            self.assertIsNotNone(storage.reason_unusable(d,set()))
        d=disk();d["children"][0]["children"]=[dict(type="crypt",path="/dev/dm-0")]
        self.assertIsNotNone(storage.reason_unusable(d,set()))

    def test_external_reader_and_native_mmc_are_detected_but_not_written(self):
        for d in (disk(),disk(rm=False,hotplug=True,tran="usb"),disk(path="/dev/mmcblk1",tran="mmc")):
            self.assertEqual(storage.candidates(storage.Inventory([d],set())),[d])
        self.assertNotIn("\x1b",storage.description(disk(model="bad\x1b[2J reader")))

    def test_inventory_protects_actual_work_download_filesystem(self):
        d=disk();real_read=Path.read_text
        def read(path,*a,**kw):
            if path.name=="diskseq":return "7"
            if str(path)=="/proc/swaps":return "Filename Type Size Used Priority\n"
            return real_read(path,*a,**kw)
        def command(argv,**kw):
            if "lsblk" in argv[0]:return SimpleNamespace(stdout=json.dumps(dict(blockdevices=[d])))
            target=argv[argv.index("--target")+1]
            major="8:17" if target==str(self.state) else "0:999"
            return SimpleNamespace(stdout=json.dumps(dict(filesystems=[{"maj:min":major,"target":target}])))
        with patch.object(storage,"run",side_effect=command),patch.object(Path,"glob",return_value=[]),patch.object(Path,"read_text",read):
            snapshot=storage.inventory((self.state,))
        self.assertIn("8:17",snapshot.protected);self.assertEqual(storage.candidates(snapshot),[])

    def test_wrong_confirmation_never_calls_any_privileged_operation(self):
        with patch.object(storage,"run") as run,patch.object(storage,"check_identity") as check:
            for value in ("", "yes", "ERASE /dev/sda", "erase /dev/sdb"):
                with self.assertRaises(storage.SetupError):storage.format_card(disk(),(),value)
            run.assert_not_called();check.assert_not_called()

    def test_reused_device_name_or_changed_identity_blocks_all_writes(self):
        original=disk()
        for changed in (disk(diskseq="8"),disk(serial="other"),disk(size=32_000_000_000),None):
            with patch.object(storage,"inventory",return_value=storage.Inventory([changed] if changed else [],set())),patch.object(storage,"run") as command:
                with self.assertRaises(storage.SetupError):storage.format_card(original,(),"ERASE /dev/sdb")
                command.assert_not_called()

    def test_disk_becoming_system_storage_blocks_erase_after_selection(self):
        with patch.object(storage,"inventory",return_value=storage.Inventory([disk()],{"8:17"})),patch.object(storage,"run") as command:
            with self.assertRaises(storage.SetupError):storage.format_card(disk(),(),"ERASE /dev/sdb")
            command.assert_not_called()

    def test_format_unmounts_then_rechecks_then_only_makes_one_fat32_partition(self):
        current=disk();calls=[]
        def command(argv,**kw):
            calls.append((argv,kw))
            if len(argv)>2 and "umount" in argv[2]:current["children"][0]["mountpoints"]=[]
            return SimpleNamespace(stdout="")
        with patch.object(storage,"inventory",side_effect=lambda _:storage.Inventory([copy.deepcopy(current)],set())), \
             patch.object(storage,"block_matches",return_value=True),patch.object(storage,"system_tool",side_effect=lambda n:"/usr/bin/"+n), \
             patch.object(storage.shutil,"which",return_value=None),patch.object(storage,"run",side_effect=command):
            part=storage.format_card(disk(),(),"ERASE /dev/sdb",announce=lambda _:None)
        self.assertEqual(part["path"],"/dev/sdb1")
        actions=[args[2] for args,_ in calls if len(args)>2]
        self.assertEqual(actions,["/usr/bin/umount","/usr/bin/sfdisk","/usr/bin/mkfs.fat"])
        sfdisk_call=next((a,k) for a,k in calls if len(a)>2 and "sfdisk" in a[2]);self.assertIn("type=c",sfdisk_call[1]["input"])
        mkfs=next(a for a,_ in calls if len(a)>2 and "mkfs.fat" in a[2]);self.assertEqual(mkfs[-1],"/dev/sdb1");self.assertIn("32",mkfs)

    def test_card_swap_after_partition_table_change_prevents_mkfs(self):
        current=disk();current["children"][0]["mountpoints"]=[];calls=[]
        def command(argv,**kw):
            calls.append(argv)
            if len(argv)>2 and "sfdisk" in argv[2]:current["diskseq"]="NEW"
            return SimpleNamespace(stdout="")
        with patch.object(storage,"inventory",side_effect=lambda _:storage.Inventory([copy.deepcopy(current)],set())), \
             patch.object(storage,"block_matches",return_value=True),patch.object(storage,"system_tool",side_effect=lambda n:"/usr/bin/"+n), \
             patch.object(storage.shutil,"which",return_value=None),patch.object(storage,"run",side_effect=command):
            with self.assertRaises(storage.SetupError):storage.format_card(disk(),(),"ERASE /dev/sdb",announce=lambda _:None)
        self.assertFalse(any("mkfs.fat" in " ".join(c) for c in calls))

    def test_still_mounted_device_blocks_partitioning(self):
        with patch.object(storage,"inventory",return_value=storage.Inventory([disk()],set())), \
             patch.object(storage,"block_matches",return_value=True),patch.object(storage,"system_tool",side_effect=lambda n:"/usr/bin/"+n),patch.object(storage,"run") as command:
            with self.assertRaises(storage.SetupError):storage.format_card(disk(),(),"ERASE /dev/sdb")
        self.assertFalse(any("sfdisk" in " ".join(c.args[0]) for c in command.call_args_list))

    def test_new_sd_copy_is_verified_and_other_user_files_survive(self):
        precious=self.card/"photos.txt";precious.write_text("keep")
        storage.copy_package(self.package,self.card,progress=lambda _:None)
        self.assertEqual(precious.read_text(),"keep")
        self.assertEqual((self.card/"pos-install/firmware.bin").read_bytes(),(self.package/"firmware.bin").read_bytes())
        self.assertFalse((self.card/"pos-install.copying").exists())
        storage.copy_package(self.package,self.card,progress=lambda _:None)

    def test_copy_refuses_existing_pos_different_installer_and_partial(self):
        for folder in ("POS","pos-install","pos-install.copying"):
            with self.subTest(folder=folder):
                path=self.card/folder;path.mkdir();(path/"precious").write_text("keep")
                with self.assertRaises(storage.SetupError):storage.copy_package(self.package,self.card,progress=lambda _:None)
                self.assertEqual((path/"precious").read_text(),"keep");shutil.rmtree(path)

    def test_copy_failure_never_publishes_partial_as_complete(self):
        original=storage.sha256
        def corrupt(path):return "0"*64 if "pos-install.copying" in str(path) else original(path)
        with patch.object(storage,"sha256",side_effect=corrupt):
            with self.assertRaises(storage.SetupError):storage.copy_package(self.package,self.card,progress=lambda _:None)
        self.assertFalse((self.card/"pos-install").exists());self.assertTrue((self.card/"pos-install.copying").is_dir())

    def test_copy_rejects_links_recursive_source_and_low_space(self):
        (self.package/"link").symlink_to(self.package/"firmware.bin")
        with self.assertRaises(storage.SetupError):storage.copy_package(self.package,self.card)
        (self.package/"link").unlink()
        with patch.object(storage.shutil,"disk_usage",return_value=SimpleNamespace(free=1)):
            with self.assertRaises(storage.SetupError):storage.copy_package(self.package,self.card)
        inside=self.card/"source";shutil.copytree(self.package,inside)
        with self.assertRaises(storage.SetupError):storage.copy_package(inside,self.card)

    def test_crostini_folder_flow_cannot_format_or_request_privilege(self):
        with patch("builtins.input",side_effect=["1","y"]),patch.object(storage,"card_folder_type",return_value="9p"),patch.object(wizard.storage,"format_card") as format_card,patch.object(wizard.storage,"run") as privileged:
            wizard.prepare_card(self.package,self.state,self.card)
        format_card.assert_not_called();privileged.assert_not_called()
        self.assertTrue((self.card/"pos-install/firmware.bin").is_file())

    def test_unknown_shared_filesystem_requires_explicit_fat32_confirmation(self):
        with patch("builtins.input",side_effect=["1","n"]),patch.object(storage,"card_folder_type",return_value="9p"):
            with self.assertRaises(storage.SetupError):wizard.prepare_card(self.package,self.state,self.card)
        self.assertFalse((self.card/"pos-install").exists())

    def test_private_backup_directory_links_and_public_permissions_refused(self):
        folder=self.root/"bad";folder.mkdir();folder.chmod(0o755)
        with self.assertRaises(storage.SetupError):wizard.private_directory(folder)
        link=self.root/"link";link.symlink_to(self.state,target_is_directory=True)
        with self.assertRaises(storage.SetupError):wizard.private_directory(link)

    def test_flash_cancel_cannot_reach_firmware_write(self):
        with patch("builtins.input",side_effect=["","NO"]),patch.object(wizard,"serial_port",return_value="/dev/ttyACM0"),patch("pos_deploy.flash") as write:
            with self.assertRaises(storage.SetupError):wizard.flash(self.root,self.state,"/dev/ttyACM0",self.root/"factory.bin")
            write.assert_not_called()

    def fake_backup(self,port,path,device_id="aa:bb:cc:dd:ee:ff"):
        path.write_bytes(b"verified-backup")
        Path(str(path)+".json").write_text(json.dumps(dict(device_id=device_id)))

    def test_factory_boot_observation_is_required_before_pos_flash(self):
        probe=dict(device_id="aa:bb:cc:dd:ee:ff",plain_development_flash=True)
        with patch.object(wizard,"serial_port",return_value="/dev/ttyACM0"),patch("builtins.input",side_effect=["y","y","n"]), \
             patch.object(posctl,"hardware_probe",return_value=probe),patch.object(posctl,"factory_backup",side_effect=self.fake_backup),patch.object(posctl,"factory_restore") as restored:
            with self.assertRaises(storage.SetupError):wizard.backup_and_recovery(self.state)
            restored.assert_called_once()
        self.assertFalse(list(self.state.rglob("*.pos-setup-recovery.json")))

    def test_changed_cardputer_during_backup_cannot_be_restored_or_marked_proven(self):
        probe=dict(device_id="aa:bb:cc:dd:ee:ff",plain_development_flash=True)
        with patch.object(wizard,"serial_port",return_value="/dev/ttyACM0"),patch("builtins.input",return_value="y"), \
             patch.object(posctl,"hardware_probe",return_value=probe),patch.object(posctl,"factory_backup",side_effect=lambda port,path:self.fake_backup(port,path,"00:11:22:33:44:55")),patch.object(posctl,"factory_restore") as restored:
            with self.assertRaises(storage.SetupError):wizard.backup_and_recovery(self.state)
            restored.assert_not_called()

    def test_saved_original_backups_survive_and_bad_or_other_device_backups_are_skipped(self):
        folder=self.state/"backups";folder.mkdir(mode=0o700)
        backup=folder/"original.bin"
        with backup.open("wb") as f:f.truncate(4*1024*1024)
        metadata=dict(format=1,chip="esp32s3",device_id="aa:bb:cc:dd:ee:ff",bytes=backup.stat().st_size,sha256=storage.sha256(backup),readback_verified=True,plain_development_flash=True)
        Path(str(backup)+".json").write_text(json.dumps(metadata))
        (folder/"broken.bin.json").symlink_to(folder/"nonexistent")
        (folder/"odd.bin").write_bytes(b"x");(folder/"odd.bin.json").write_text("[]")
        self.assertEqual(wizard.saved_backups(folder,metadata["device_id"]),[backup])
        self.assertEqual(wizard.saved_backups(folder,"00:11:22:33:44:55"),[])
        with backup.open("r+b") as f:f.write(b"corruption")
        self.assertEqual(wizard.saved_backups(folder,metadata["device_id"]),[])
        self.assertTrue(backup.exists())

    def test_live_usb_progress_captures_identity_and_restores_embedded_helper(self):
        process=Mock();process.stdout=io.StringIO("MAC: aa:bb:cc:dd:ee:ff\nWriting 100%\n");process.wait.return_value=0;process.poll.return_value=0
        original=posctl.hardware_command
        with patch.object(wizard.subprocess,"Popen",return_value=process):
            with wizard.live_usb():
                text=posctl.hardware_command("/dev/SIMULATED","chip_id")
                self.assertIn("MAC: aa:bb:cc:dd:ee:ff",text)
        self.assertIs(posctl.hardware_command,original)

    def test_esptool_backspace_progress_is_streamed_and_rate_limited(self):
        frames=[f"{n*4096} ({n//10} %)" for n in range(1,1000)]
        raw="esptool.py v4.12.0\n"+"".join(f+"\b"*len(f) for f in frames)+"4194304 (100 %)\n"
        process=Mock();process.stdout=io.StringIO(raw);process.wait.return_value=0;process.poll.return_value=0
        with patch.object(wizard.subprocess,"Popen",return_value=process),patch.object(wizard.time,"monotonic",return_value=100),patch.object(wizard,"say") as say:
            text=wizard.hardware_command_live("mock-port","read_flash","0","ALL","unused.bin")
        displayed="\n".join(call.args[0] for call in say.call_args_list)
        self.assertIn("4096 (0 %)",displayed);self.assertIn("4194304 (100 %)",displayed)
        self.assertLess(say.call_count,10);self.assertNotIn("?",displayed);self.assertNotIn("\b",text)
        self.assertIn("4091904 (99 %)",text)

    def test_live_usb_preserves_fatal_reason_and_raises_retryable_error(self):
        process=Mock();process.stdout=io.StringIO("458752 (5 %)\b\b\nA fatal error occurred: Corrupt data, expected 0x1000 bytes but received 0xdac bytes\n")
        process.wait.return_value=2;process.poll.return_value=2
        with patch.object(wizard.subprocess,"Popen",return_value=process),patch.object(wizard,"say"):
            with self.assertRaisesRegex(posctl.USBCommandError,"received 0xdac bytes"):
                wizard.hardware_command_live("mock-port","read_flash","0","ALL","unused.bin")

    def test_backup_failure_gives_power_cycle_help_without_restoring_or_flashing(self):
        probe=dict(device_id="aa:bb:cc:dd:ee:ff",plain_development_flash=True)
        with patch.object(wizard,"serial_port",return_value="mock-port"),patch.object(wizard,"ask",return_value=True), \
             patch.object(posctl,"hardware_probe",return_value=probe),patch.object(posctl,"factory_backup",side_effect=posctl.USBCommandError("ROM read failed")), \
             patch.object(posctl,"factory_restore") as restore:
            with self.assertRaisesRegex(storage.SetupError,"POS has NOT been flashed") as error:
                wizard.backup_and_recovery(self.state)
        self.assertIn("wait 10 seconds",str(error.exception));self.assertIn("Incomplete files",str(error.exception))
        restore.assert_not_called()

    def test_copy_card_change_keeps_partial_and_cannot_publish(self):
        guard=Mock(side_effect=[None,None,storage.SetupError("card removed")])
        with self.assertRaises(storage.SetupError):storage.copy_package(self.package,self.card,progress=lambda _:None,check=guard)
        self.assertTrue((self.card/"pos-install.copying").is_dir());self.assertFalse((self.card/"pos-install").exists())

    def test_existing_verified_backup_can_recover_an_interrupted_attempt(self):
        folder=self.state/"backups";folder.mkdir(mode=0o700);backup=folder/"original.bin"
        with backup.open("wb") as f:f.truncate(4*1024*1024)
        metadata=dict(format=1,chip="esp32s3",device_id="aa:bb:cc:dd:ee:ff",bytes=backup.stat().st_size,sha256=storage.sha256(backup),readback_verified=True,plain_development_flash=True)
        Path(str(backup)+".json").write_text(json.dumps(metadata))
        with patch.object(wizard,"serial_port",return_value="/dev/ttyACM0"),patch("builtins.input",side_effect=["y","1","y","y"]), \
             patch.object(posctl,"hardware_probe",return_value=metadata),patch.object(posctl,"factory_backup") as new,patch.object(posctl,"factory_restore") as restored:
            port,selected=wizard.backup_and_recovery(self.state)
            self.assertEqual(selected,backup);new.assert_not_called();restored.assert_called_once_with(port,backup,True)
        proof=json.loads(Path(str(backup)+".pos-setup-recovery.json").read_text())
        self.assertTrue(proof["factory_boot_observed"]);self.assertEqual(proof["backup_sha256"],storage.sha256(backup))
        original=Path(str(backup)+".pos-setup-recovery.json").read_bytes()
        with patch.object(wizard,"serial_port",return_value="/dev/ttyACM0"),patch("builtins.input",side_effect=["y","1","y","y"]), \
             patch.object(posctl,"hardware_probe",return_value=metadata),patch.object(posctl,"factory_backup") as new,patch.object(posctl,"factory_restore") as restored:
            port,selected=wizard.backup_and_recovery(self.state)
            self.assertEqual(selected,backup);new.assert_not_called();restored.assert_called_once_with(port,backup,True)
        self.assertEqual(Path(str(backup)+".pos-setup-recovery.json").read_bytes(),original)
        repeated=list(folder.glob("original.bin.pos-setup-recovery-*.json"))
        self.assertEqual(len(repeated),1);self.assertEqual(repeated[0].stat().st_mode&0o777,0o600)
        self.assertTrue(json.loads(repeated[0].read_text())["factory_boot_observed"])

    def test_known_exfat_ext4_and_card_subfolders_cannot_masquerade_as_fat32(self):
        for kind in ("ext4","exfat","ntfs"):
            result=SimpleNamespace(stdout=json.dumps(dict(filesystems=[dict(fstype=kind,target=str(self.card))])))
            with patch.object(storage,"run",return_value=result):
                with self.assertRaises(storage.SetupError):storage.card_folder_type(self.card)
        result=SimpleNamespace(stdout=json.dumps(dict(filesystems=[dict(fstype="vfat",target=str(self.card.parent))])))
        with patch.object(storage,"run",return_value=result):
            with self.assertRaises(storage.SetupError):storage.card_folder_type(self.card)
        result=SimpleNamespace(stdout=json.dumps(dict(filesystems=[dict(fstype="vfat",target=str(self.card))])))
        with patch.object(storage,"run",return_value=result):self.assertEqual(storage.card_folder_type(self.card),"vfat")

    def test_unreadable_active_virtual_disk_trace_disables_format_discovery(self):
        backing=Mock();backing.read_text.side_effect=PermissionError("hidden loop backing")
        data=SimpleNamespace(stdout=json.dumps(dict(blockdevices=[disk()])))
        with patch.object(storage,"run",return_value=data) as command,patch.object(Path,"glob",return_value=[backing]):
            with self.assertRaises(storage.SetupError):storage.inventory(())
            self.assertEqual(command.call_count,1)

    def test_missing_loop_backing_in_this_namespace_disables_format_discovery(self):
        backing=Mock();backing.read_text.return_value="/this-loop-file-is-not-visible-in-the-container.img"
        data=SimpleNamespace(stdout=json.dumps(dict(blockdevices=[disk()])))
        with patch.object(storage,"run",return_value=data) as command,patch.object(Path,"glob",return_value=[backing]):
            with self.assertRaises(storage.SetupError):storage.inventory(())
            self.assertEqual(command.call_count,1)

    def test_executable_runs_without_checkout_and_doctor_has_no_mutations(self):
        out=self.root/"standalone/pos-setup";build_setup.build(out)
        result=subprocess.run([str(out),"--doctor"],cwd=self.root,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("No downloads, installs, formatting, mounts or flash writes",result.stdout)
        self.assertEqual(out.stat().st_mode&0o777,0o755)
        with self.assertRaises(ValueError):build_setup.build(out)
        self.assertEqual(subprocess.run([str(out),"--version"],capture_output=True,text=True).stdout.strip(),"POS Setup "+wizard.VERSION)


if __name__ == "__main__":unittest.main()
