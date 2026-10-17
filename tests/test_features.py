import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib
from test_installer import image,posctl

ROOT=Path(__file__).resolve().parents[1]
BACKUP="POS-Filesystem-Backup.zip"

def archive_bytes(files,metadata=None):
    """Independent standards-based writer for adversarial restore fixtures."""
    meta={"format":1,"migration":1,"source_version":"0.1.0-dev","source_bytes":sum(len(data) for _,data in files),"files":len(files)}
    if metadata:meta.update(metadata)
    entries=[("POS-Backup.json",json.dumps(meta).encode())]+files
    local=bytearray();central=bytearray()
    for name,data in entries:
        name=name.encode();crc=zlib.crc32(data);offset=len(local)
        local.extend(struct.pack("<IHHHHHIIIHH",0x04034b50,20,0x808,0,0,0x21,0,0,0,len(name),0));local.extend(name);local.extend(data)
        local.extend(struct.pack("<IIII",0x08074b50,crc,len(data),len(data)))
        central.extend(struct.pack("<IHHHHHHIIIHHHHHII",0x02014b50,20,20,0x808,0,0,0x21,crc,len(data),len(data),len(name),0,0,0,0,0,offset));central.extend(name)
    return bytes(local+central+struct.pack("<IHHHHIIH",0x06054b50,0,0,len(entries),len(entries),len(central),len(local),0))

class FeatureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compilation=tempfile.TemporaryDirectory(prefix="pos-features-native-")
        cls.runner=Path(cls.compilation.name)/"features"
        sdk=Path(os.environ.get("IDF_PATH","/workspace/pos-cloud/esp-idf-v5.5.5"));cjson=sdk/"components/json/cJSON"
        sources=[ROOT/"tests/feature_runner.c",ROOT/"main/ui.c",ROOT/"main/setup.c",ROOT/"main/settings.c",ROOT/"main/browser.c",ROOT/"main/background.c",ROOT/"main/editor.c",ROOT/"main/wifi.c",ROOT/"components/pos_device/pos_console.c",ROOT/"components/pos_install/pos_install.c",ROOT/"components/pos_install/pos_hash.c",cjson/"cJSON.c"]
        sources += [ROOT/"main/files.c",ROOT/"main/music.c",ROOT/"main/recorder.c",ROOT/"main/instruments.c",ROOT/"main/sharing.c",ROOT/"main/expansion.c"]
        for component in ("pos_core","pos_recovery"):
            sources.extend((ROOT/"components"/component).glob("*.c"))
        command=["gcc","-std=c11","-D_POSIX_C_SOURCE=200809L","-Wall","-Wextra","-Werror","-g","-fsanitize=undefined","-fno-sanitize-recover=all"]
        for component in ("pos_install","pos_core","pos_recovery"):
            command.extend(["-I",str(ROOT/"components"/component/"include")])
        command.extend(["-I",str(ROOT/"tests/stubs"),"-I",str(ROOT/"main"),"-I",str(ROOT/"components/pos_device/include")])
        subprocess.run(command+["-I",str(cjson),*map(str,sources),"-lcrypto","-lm","-o",str(cls.runner)],check=True)

    @classmethod
    def tearDownClass(cls):cls.compilation.cleanup()

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="pos-feature-test-");self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name);self.card=self.base/"card";self.card.mkdir()

    def write(self,name,data=b"user contents"):
        p=self.card/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);return p

    def run_feature(self,command,*args,expected=0,card=None):
        result=subprocess.run([str(self.runner),str(card or self.card),command,*map(str,args)],capture_output=True,text=True)
        self.assertEqual(result.returncode,expected,result.stdout+result.stderr);return result

    def seed(self):
        self.files={"Documents/Stevie.txt":b"words\n"*20000,"Music/song.dat":bytes(range(256))*70,"POS/Users/Owner/Saves/game.bin":b"save", "loose file.txt":b"loose"}
        for name,data in self.files.items():self.write(name,data)
        for name in ("POS/System/core.bin","POS/Temp/scratch","POS/Cache/map","POS/Updates/download","pos-install/payload/file","POS.previous/old","POS/.pos-transaction"):
            self.write(name,b"not user data")

    def destination(self):p=self.base/"restored";p.mkdir();return p

    def test_large_collection_automatically_uses_readable_folder_backup(self):
        for i in range(2050): self.write(f"Documents/{i:05}.txt", str(i).encode())
        scan=json.loads(self.run_feature("scan").stdout);self.assertEqual(scan["files"],2050)
        self.run_feature("backup");backup=self.card/"POS-Filesystem-Backup"
        self.assertTrue(backup.is_dir());self.assertFalse((self.card/BACKUP).exists())
        self.assertEqual((backup/"files/Documents/02049.txt").read_text(),"2049")
        self.run_feature("verify");self.run_feature("match")
        dest=self.destination();self.run_feature("restore","--free",2**40,"--archive",backup,card=dest)
        self.assertEqual((dest/"Documents/02049.txt").read_text(),"2049")

    def test_large_total_and_fat32_file_sizes_scan_without_loading_data(self):
        for i in range(17):
            p=self.card/f"large-{i}.bin"
            with p.open("wb") as f:f.truncate(0xffffffff)
        report=json.loads(self.run_feature("scan").stdout)
        self.assertEqual(report["bytes"],17*0xffffffff)
        self.assertGreater(report["bytes"],64*1024**3)

    def test_folder_backup_preserves_empty_directories_and_detects_extra_missing_changed_files(self):
        self.seed();(self.card/"empty folder/nested").mkdir(parents=True)
        self.run_feature("backup");backup=self.card/"POS-Filesystem-Backup"
        self.assertTrue((backup/"files/empty folder/nested").is_dir());self.run_feature("verify")
        dest=self.destination();self.run_feature("restore","--archive",backup,card=dest)
        self.assertTrue((dest/"empty folder/nested").is_dir())
        p=backup/"files/loose file.txt";original=p.read_bytes();p.write_bytes(b"corruption")
        self.run_feature("verify",expected=1);p.write_bytes(original)
        extra=backup/"files/extra.txt";extra.write_text("extra");self.run_feature("verify",expected=1);extra.unlink()
        p.unlink();self.run_feature("verify",expected=1)

    def test_folder_backup_power_cuts_cancellation_and_low_space_preserve_originals(self):
        for phase in ("backup","backup-finalize","verify"):
            with self.subTest(phase=phase):
                card=self.base/phase;card.mkdir();(card/"document.txt").write_bytes(b"words"*5000)
                self.run_feature("backup-set","--interrupt",phase,card=card,expected=99)
                self.assertFalse((card/"POS-Filesystem-Backup").exists())
                self.assertTrue((card/"POS-Filesystem-Backup.part").is_dir())
                self.assertEqual((card/"document.txt").read_bytes(),b"words"*5000)
        self.write("document.txt",b"data")
        self.run_feature("backup-set","--free",1,expected=1)
        self.assertFalse((self.card/"POS-Filesystem-Backup.part").exists())

    def test_folder_backup_resumes_owned_partial_copies_and_retains_interrupted_bytes(self):
        self.seed();self.run_feature("backup-set","--interrupt","backup",expected=99)
        stage=self.card/"POS-Filesystem-Backup.part"
        self.run_feature("resume-backup");self.run_feature("verify");self.run_feature("match")
        backup=self.card/"POS-Filesystem-Backup"
        self.assertFalse(stage.exists());self.assertTrue((backup/"interrupted").is_dir())
        for name,data in self.files.items(): self.assertEqual((backup/"files"/name).read_bytes(),data)

    def test_unknown_partial_backup_and_future_state_are_never_overwritten(self):
        self.write("document.txt",b"words")
        stage=self.card/"POS-Filesystem-Backup.part";stage.mkdir();(stage/"precious").write_text("only copy")
        self.run_feature("resume-backup",expected=1)
        self.assertEqual((stage/"precious").read_text(),"only copy")
        state={"format":99,"version":"0.2.0-dev","files":1,"directories":0,"bytes":5}
        (stage/".pos-backup-state.json").write_text(json.dumps(state));self.run_feature("resume-backup",expected=1)
        self.assertEqual((stage/"precious").read_text(),"only copy")

    def test_file_over_two_gib_can_begin_streaming_without_archive_seek(self):
        p=self.card/"big.bin"
        with p.open("wb") as f:f.truncate(0x80000001)
        self.run_feature("backup","--free",2**40,"--interrupt","backup",expected=99)
        self.assertEqual(p.stat().st_size,0x80000001)
        self.assertFalse((self.card/"POS-Filesystem-Backup").exists())

    def test_folder_restore_validates_all_data_before_writes_and_resumes_after_interruption(self):
        self.seed();self.run_feature("backup-set");backup=self.card/"POS-Filesystem-Backup";dest=self.destination()
        self.run_feature("restore","--archive",backup,"--interrupt","restore",card=dest,expected=99)
        self.run_feature("restore","--archive",backup,card=dest)
        for name,data in self.files.items(): self.assertEqual((dest/name).read_bytes(),data)
        reports=list((dest/"POS/Users/Owner/Recovery").glob("*.jsonl"));self.assertTrue(reports)
        self.assertEqual(json.loads(reports[0].read_text().splitlines()[-1])["action"],"restore complete")
        (backup/"files/loose file.txt").write_text("changed")
        untouched=self.base/"untouched";untouched.mkdir()
        self.run_feature("restore","--archive",backup,card=untouched,expected=1)
        self.assertEqual(list(untouched.iterdir()),[])

    def test_corrupt_current_settings_are_quarantined_for_both_backup_formats(self):
        for mode in ("backup","backup-set"):
            with self.subTest(mode=mode):
                card=self.base/mode;card.mkdir();p=card/"POS/Users/settings.1.json";p.parent.mkdir(parents=True)
                p.write_text(json.dumps({"payload":json.dumps({"format":1}),"sha256":"0"*64}))
                self.run_feature(mode,card=card)
                archive=card/(BACKUP if mode=="backup" else "POS-Filesystem-Backup")
                dest=self.base/(mode+"-restore");dest.mkdir()
                self.run_feature("restore","--archive",archive,card=dest)
                self.assertFalse((dest/"POS/Users/settings.1.json").exists())
                self.assertTrue(list((dest/"POS/Recovered").rglob("settings.1.json")))

    def test_current_settings_restore_selects_latest_valid_generation_and_preserves_existing_preferences(self):
        self.run_feature("settings-save","--name","Older")
        self.run_feature("settings-save","--name","Latest")
        self.run_feature("backup-set");backup=self.card/"POS-Filesystem-Backup"
        dest=self.destination();self.run_feature("restore","--archive",backup,card=dest)
        self.assertEqual(json.loads(self.run_feature("settings-read",card=dest).stdout)["name"],"Latest")
        other=self.base/"existing";other.mkdir();self.run_feature("settings-save","--name","Current user",card=other)
        self.run_feature("restore","--archive",backup,card=other)
        self.assertEqual(json.loads(self.run_feature("settings-read",card=other).stdout)["name"],"Current user")
        self.assertEqual(len(list((other/"POS/Recovered").rglob("settings.*.json"))),2)

    def test_folder_backup_reinstall_retirement_resumes_and_restores(self):
        self.installed();self.run_feature("backup-set")
        package=self.package("0.2.0-dev","new system");shutil.copytree(package,self.card/"pos-install")
        self.run_feature("reinstall","--interrupt","retired",expected=99)
        self.run_feature("reinstall");self.run_feature("boot");self.run_feature("restore")
        self.assertEqual((self.card/"POS/System/release.txt").read_text(),"new system")
        self.assertEqual((self.card/"POS/Users/Owner/Documents/Stevie.txt").read_bytes(),b"the original document")
        self.assertTrue((self.card/"POS-Filesystem-Backup/files").is_dir())

    def test_mid_write_failure_never_publishes_backup_or_changes_originals(self):
        for mode in ("backup","backup-set"):
            card=self.base/mode;card.mkdir();source=card/"document.bin";source.write_bytes(b"abc"*40000)
            self.run_feature(mode,"--file-limit",32000,card=card,expected=1)
            self.assertFalse((card/BACKUP).exists());self.assertFalse((card/"POS-Filesystem-Backup").exists())
            self.assertEqual(source.read_bytes(),b"abc"*40000)

    def test_interrupted_migration_can_resume_without_losing_original_settings(self):
        self.run_feature("settings-save","--name","Stevie");self.run_feature("backup-set")
        archive=self.card/"POS-Filesystem-Backup"
        for phase in ("migration","migration-applied"):
            dest=self.base/phase;dest.mkdir()
            self.run_feature("restore","--archive",archive,"--interrupt",phase,card=dest,expected=99)
            self.assertTrue(archive.is_dir());self.run_feature("restore","--archive",archive,card=dest)
            self.assertEqual(json.loads(self.run_feature("settings-read",card=dest).stdout)["name"],"Stevie")
            self.assertTrue(list((dest/"POS/Recovered").rglob("settings.1.json")))

    def test_journaled_reinstall_rechecks_changed_data_before_retirement(self):
        self.installed();self.run_feature("backup-set")
        package=self.package("0.2.0-dev","new system");shutil.copytree(package,self.card/"pos-install")
        self.run_feature("reinstall","--interrupt","reinstall_journaled",expected=99)
        self.assertTrue((self.card/"POS").is_dir());self.assertFalse((self.card/"POS.previous").exists())
        self.write("POS/Users/new.txt",b"new data after journal")
        self.run_feature("reinstall",expected=1)
        self.assertFalse((self.card/"POS.previous").exists())
        self.assertEqual((self.card/"POS/Users/new.txt").read_bytes(),b"new data after journal")

    def test_zip_restores_latest_settings_without_replacing_existing_preferences(self):
        self.run_feature("settings-save","--name","Older");self.run_feature("settings-save","--name","Latest")
        self.run_feature("backup");dest=self.destination()
        self.run_feature("restore","--archive",self.card/BACKUP,card=dest)
        self.assertEqual(json.loads(self.run_feature("settings-read",card=dest).stdout)["name"],"Latest")
        self.run_feature("settings-save","--name","Current user",card=dest)
        self.run_feature("restore","--archive",self.card/BACKUP,card=dest)
        self.assertEqual(json.loads(self.run_feature("settings-read",card=dest).stdout)["name"],"Current user")

    def test_folder_backup_rejects_case_collisions_symlinks_and_future_metadata(self):
        self.write("A",b"user file");self.write("a/child",b"other user file")
        self.run_feature("backup-set",expected=1);self.assertFalse((self.card/"POS-Filesystem-Backup.part").exists())
        (self.card/"A").unlink();self.run_feature("backup-set")
        archive=self.card/"POS-Filesystem-Backup";meta=archive/"backup.json"
        original=meta.read_text();future=json.loads(original);future["format"]=99;meta.write_text(json.dumps(future))
        dest=self.destination();self.run_feature("restore","--archive",archive,card=dest,expected=1)
        self.assertEqual(list(dest.iterdir()),[]);meta.write_text(original)
        target=self.base/"outside";target.write_text("private")
        (archive/"files/symlink").symlink_to(target);self.run_feature("verify",expected=1)

    def test_console_poll_does_not_block_physical_input_supervisor(self):
        self.assertTrue(json.loads(self.run_feature("console").stdout)["nonblocking"])

    def test_completed_but_unpublished_folder_backup_can_resume(self):
        self.seed();self.run_feature("backup-set","--interrupt","backup-finalize",expected=99)
        stage=self.card/"POS-Filesystem-Backup.part"
        self.assertTrue((stage/"backup.json").exists())
        self.run_feature("resume-backup");self.run_feature("verify");self.run_feature("match")
        self.assertTrue((self.card/"POS-Filesystem-Backup/interrupted").is_dir())

    def test_editor_ui_types_wasd_as_text_saves_and_closes_with_home(self):
        self.run_feature("editor-ui-save")
        self.assertEqual((self.card/"POS/Users/Owner/Documents/Untitled.txt").read_bytes(),b"wasd")

    def test_stock_fn_delete_removes_character_ahead_of_editor_cursor(self):
        self.run_feature("editor-ui-delete")
        self.assertEqual((self.card/"POS/Users/Owner/Documents/Untitled.txt").read_bytes(),b"asd")

    def test_physical_stock_fn_arrows_and_enter_operate_black_installer_menu(self):
        self.run_feature("keyboard-ui")

    def test_entire_stock_adv_key_layout_normal_shift_and_fn_layers(self):
        # Stock UserDemo keyboard.cpp b549eac0...: physical TCA codes by row,
        # normal/Shift character legends and Fn overrides. No decoder-derived
        # expectations; in particular stock Fn+number stays a number.
        scans=[[1,5,11,15,21,25,31,35,41,45,51,55,61,65],
               [2,6,12,16,22,26,32,36,42,46,52,56,62,66],
               [3,7,13,17,23,27,33,37,43,47,53,57,63,67],
               [4,8,14,18,24,28,34,38,44,48,54,58,64,68]]
        normal=[list("`1234567890-=")+[258],list("\tqwertyuiop[]\\"),
                [None,None,*"asdfghjkl;'",256],[None,None,None,*"zxcvbnm,./ "]]
        shifted=[list("~!@#$%^&*()_+")+[258],list("\tQWERTYUIOP{}|"),
                 [None,None,*'ASDFGHJKL:"',256],[None,None,None,*"ZXCVBNM<>? "]]
        overrides={1:257,65:264,57:259,54:261,58:260,64:262}
        for row in range(4):
            for column in range(14):
                code=scans[row][column];base=normal[row][column]
                for layer,prefix,mods in [("normal",[],0),("shift",[0x87],1),("fn",[0x83],4)]:
                    with self.subTest(code=code,layer=layer):
                        actual=[tuple(map(int,line.split())) for line in self.run_feature("keyboard-events",*prefix,code|128,code).stdout.splitlines()]
                        if base is None:expected=[]
                        elif layer=="fn" and code==68:expected=[(263,4,1)] # POS global Home.
                        else:
                            key=shifted[row][column] if layer=="shift" else base
                            if layer=="fn":
                                key=overrides.get(code,base)
                                if isinstance(key,str) and key.isalpha():key=key.upper();mods|=1
                            if isinstance(key,str):key=ord(key)
                            expected=[(key,mods,1),(key,4 if layer=="fn" else mods,0)]
                        self.assertEqual(actual,expected)

    def test_key_releases_keep_pressed_identity_when_modifiers_are_released_first(self):
        actual=self.run_feature("keyboard-events",0x83,0xb9,0x03,0x39).stdout
        self.assertEqual(actual,"259 4 1\n259 0 0\n")
        actual=self.run_feature("keyboard-events",0x87,0x8c,0x07,0x0c).stdout
        self.assertEqual(actual,"87 1 1\n87 0 0\n")

    def test_stock_opt_meta_modifier_is_preserved_in_app_events(self):
        self.assertEqual(self.run_feature("keyboard-events",0x88,0x8d,0x0d,0x08).stdout,"97 16 1\n97 16 0\n")

    def test_settings_brightness_validation_save_and_cancel_preserve_other_preferences(self):
        self.run_feature("settings-ui-brightness")
        for name in ("settings-ui-cancel","settings-ui-timezone","settings-ui-safe","settings-ui-profile"):
            card=self.base/name;card.mkdir();self.run_feature(name,card=card)

    def test_editor_selection_clipboard_undo_redo_and_word_navigation_controls(self):
        self.run_feature("editor-ui-advanced")
        self.assertEqual((self.card/"POS/Users/Owner/Documents/Untitled.txt").read_bytes(),b"zwasd")

    def test_editor_unsaved_close_keeps_plain_draft_or_discards_explicitly(self):
        for mode in ("editor-ui-draft","editor-ui-discard"):
            card=self.base/mode;card.mkdir();self.run_feature(mode,card=card)
            docs=card/"POS/Users/Owner/Documents"
            self.assertFalse((docs/"Untitled.txt").exists())
            if mode=="editor-ui-draft":self.assertEqual((docs/"Draft-0.txt").read_bytes(),b"wasd")
            else:self.assertFalse(list(docs.glob("Draft-*.txt")))

    def test_wifi_ui_remembers_encrypted_profile_without_sd_credentials(self):
        self.run_feature("wifi-ui-remember")
        self.assertFalse(list(self.card.rglob("*")))

    def test_wifi_transfer_session_stops_on_global_home(self):
        self.run_feature("wifi-ui-transfer")
    def test_expansion_settings_refuses_missing_cap_and_keeps_maps_gps_gated(self):
        self.run_feature('hardware-ui-missing')
    def test_lora_setting_toggle_never_arms_tx_or_disables_detected_gps(self):
        self.run_feature('hardware-ui-toggle')
    def music_fixture(self):
        import wave
        path=self.card/'Music/tone.wav';path.parent.mkdir(exist_ok=True)
        with wave.open(str(path),'wb') as out:
            out.setnchannels(1);out.setsampwidth(2);out.setframerate(22050);out.writeframes(struct.pack('<'+'h'*4096,*([-20000,20000]*2048)))
    def test_music_closes_with_home_and_releases_audio(self):
        self.music_fixture();self.run_feature('music-ui-home')
    def test_music_space_pauses_and_resumes_without_leaving_audio_running(self):
        self.music_fixture();self.run_feature('music-ui-pause')
    def test_file_manager_copy_and_cut_paste_actions(self):
        for action in ('copy','cut'):
            with self.subTest(action=action):
                card=self.base/action;folder=card/'POS/Users/Owner';folder.mkdir(parents=True);(folder/'a.txt').write_bytes(b'original contents'*5000);(folder/'destination').mkdir()
                self.run_feature('files-ui-'+action,card=card)
                self.assertEqual((folder/'destination/a.txt').read_bytes(),b'original contents'*5000)
                self.assertEqual((folder/'a.txt').exists(),action=='copy')
    def test_permanent_delete_requires_exact_confirmation(self):
        self.write('POS/Users/Owner/a.txt',b'keep');self.run_feature('files-ui-delete-cancel');self.assertEqual((self.card/'POS/Users/Owner/a.txt').read_bytes(),b'keep')
    def test_ble_receiving_requires_pairing_and_offer_acceptance_then_stops(self):
        self.run_feature('ble-ui-receive')
    def test_ble_home_rejects_pairing_and_stops_without_accepting_file(self):
        self.run_feature('ble-ui-home')

    def test_arrow_color_picker_accept_cancel_fine_adjustment_and_bounds(self):
        report=json.loads(self.run_feature("picker").stdout)
        self.assertTrue(all(report.values()))

    def test_launcher_controls_all_stripe_counts_and_black_white_icons(self):
        report=json.loads(self.run_feature("launcher").stdout)
        self.assertTrue(all(report.values()))

    def test_background_app_saves_draft_and_persists_it_after_restart(self):
        self.run_feature("settings-save","--name","Keep my name")
        self.run_feature("background-save")
        saved=json.loads(self.run_feature("settings-read").stdout)
        self.assertEqual(saved["name"],"Keep my name")
        self.assertEqual(len(saved["background"]),4)
        self.assertEqual(saved["background"][-1],0xffffff)
        self.run_feature("settings-save","--name","New display name")
        self.assertEqual(json.loads(self.run_feature("settings-read").stdout)["background"],saved["background"])
        latest=max((self.card/"POS/Users").glob("settings.[01].json"),key=lambda p:json.loads(json.loads(p.read_text())["payload"])["generation"])
        latest.write_text("torn write")
        self.assertEqual(json.loads(self.run_feature("settings-read").stdout)["background"],saved["background"])

    def test_background_cancel_home_and_invalid_count_preserve_saved_preferences(self):
        self.run_feature("settings-save")
        before={p.name:p.read_bytes() for p in (self.card/"POS/Users").glob("settings.*.json")}
        for mode in ("background-cancel","background-home","background-invalid-count"):
            with self.subTest(mode=mode):
                self.run_feature(mode)
                self.assertEqual({p.name:p.read_bytes() for p in (self.card/"POS/Users").glob("settings.*.json")},before)

    def test_background_failed_save_keeps_previous_background_and_unknown_partial(self):
        self.run_feature("settings-save")
        before=json.loads(self.run_feature("settings-read").stdout)
        partial=self.write("POS/Users/settings.0.json.part/precious.txt",b"keep this")
        self.run_feature("background-save-failure")
        self.assertEqual(json.loads(self.run_feature("settings-read").stdout),before)
        self.assertEqual(partial.read_bytes(),b"keep this")

    def test_v1_settings_keep_identity_and_gain_default_background_when_upgraded(self):
        self.run_feature("settings-save","--name","Legacy identity")
        path=self.card/"POS/Users/settings.1.json"
        envelope=json.loads(path.read_text());body=json.loads(envelope["payload"])
        body["format"]=1;del body["background"]
        envelope["payload"]=json.dumps(body);envelope["sha256"]=hashlib.sha256(envelope["payload"].encode()).hexdigest()
        path.write_text(json.dumps(envelope));before=path.read_bytes()
        read=json.loads(self.run_feature("settings-read").stdout)
        self.assertEqual(read["name"],"Legacy identity")
        self.assertEqual(read["background"],[0xff218c,0xffd800,0x21b1ff])
        self.assertEqual(path.read_bytes(),before)
        self.run_feature("settings-save")
        upgraded=json.loads(json.loads((self.card/"POS/Users/settings.0.json").read_text())["payload"])
        self.assertEqual(upgraded["format"],2)
        self.assertEqual(upgraded["background"],["#FF218C","#FFD800","#21B1FF"])

    def test_invalid_background_settings_are_rejected_without_modification(self):
        self.run_feature("settings-save")
        path=self.card/"POS/Users/settings.1.json";original=json.loads(path.read_text())
        for bad in ([],["#000000"]*13,["#FFFFFG"],["#00000000"],[123],"#FF0000"):
            with self.subTest(background=bad):
                envelope=original.copy();body=json.loads(envelope["payload"]);body["background"]=bad
                envelope["payload"]=json.dumps(body);envelope["sha256"]=hashlib.sha256(envelope["payload"].encode()).hexdigest()
                path.write_text(json.dumps(envelope));before=path.read_bytes()
                self.run_feature("settings-read",expected=1)
                self.assertEqual(path.read_bytes(),before)

    def test_background_preferences_survive_zip_and_folder_backup_restore(self):
        self.run_feature("settings-save");self.run_feature("background-save")
        saved=json.loads(self.run_feature("settings-read").stdout)["background"]
        for mode in ("backup","backup-set"):
            with self.subTest(mode=mode):
                source=self.base/(mode+"-source");shutil.copytree(self.card,source)
                self.run_feature(mode,card=source)
                destination=self.base/mode;destination.mkdir()
                archive=source/(BACKUP if mode=="backup" else "POS-Filesystem-Backup")
                self.run_feature("restore","--archive",archive,card=destination)
                self.assertEqual(json.loads(self.run_feature("settings-read",card=destination).stdout)["background"],saved)

    def test_boot_slides_yellow_then_pink_and_fades_large_centered_pos(self):
        report=json.loads(self.run_feature("boot-splash").stdout)
        for key in ("yellow_first","pink_staggered","smooth_fade","large_centered_letters"):
            self.assertTrue(report[key])
        self.assertEqual(report["splash_ms"],1000)

    def test_animated_preview_matches_device_frames_and_timings(self):
        output=self.base/"boot-animation.png"
        subprocess.run([sys.executable,str(ROOT/"tools/posctl.py"),"preview",
                        "--screen","boot","--animate","--output",str(output)],check=True)
        png=output.read_bytes();self.assertEqual(png[:8],b"\x89PNG\r\n\x1a\n")
        offset=8;sequence=0;holds=[];frames=[];payload=None
        while offset<len(png):
            size=struct.unpack_from(">I",png,offset)[0]
            kind=png[offset+4:offset+8];data=png[offset+8:offset+8+size]
            self.assertEqual(zlib.crc32(kind+data),struct.unpack_from(">I",png,offset+8+size)[0])
            offset+=size+12
            if kind==b"acTL":self.assertEqual(struct.unpack(">II",data),(29,0))
            if kind==b"fcTL":
                if payload is not None:frames.append(zlib.decompress(payload))
                seq,width,height,x,y,delay,denom,dispose,blend=struct.unpack(">IIIIIHHBB",data)
                self.assertEqual((seq,width,height,x,y,denom,dispose,blend),(sequence,240,135,0,0,1000,0,0))
                sequence+=1;holds.append(delay);payload=b""
            if kind==b"IDAT":payload+=data
            if kind==b"fdAT":
                self.assertEqual(struct.unpack_from(">I",data)[0],sequence)
                sequence+=1;payload+=data[4:]
            if kind==b"IEND":
                frames.append(zlib.decompress(payload));break
        self.assertEqual(offset,len(png));self.assertEqual(len(frames),29)
        self.assertEqual(holds,[180]*3+[40]*25+[1000])
        for frame in frames:
            self.assertEqual(len(frame),135*721)
            self.assertTrue(all(frame[y*721]==0 for y in range(135)))
        def pixel(frame,x,y):return tuple(frames[frame][y*721+1+x*3:y*721+4+x*3])
        pink,yellow,blue=[pixel(i,239,134) for i in range(3)]
        self.assertEqual(pixel(7,119,60),yellow);self.assertEqual(pixel(7,120,60),blue)
        self.assertEqual(pixel(7,0,0),blue)
        self.assertEqual(pixel(8,23,0),pink);self.assertEqual(pixel(8,24,0),blue)
        self.assertEqual(pixel(17,30,22),pink)
        self.assertNotIn(pixel(18,30,22),(pink,(0,0,0)))
        self.assertEqual(pixel(28,30,22),(0,0,0));self.assertEqual(frames[27],frames[28])
        static=self.base/"boot-splash.png"
        subprocess.run([sys.executable,str(ROOT/"tools/posctl.py"),"preview",
                        "--screen","boot","--output",str(static)],check=True)
        data=static.read_bytes();offset=8;payload=b""
        while offset<len(data):
            size=struct.unpack_from(">I",data,offset)[0]
            if data[offset+4:offset+8]==b"IDAT":payload+=data[offset+8:offset+8+size]
            offset+=size+12
        self.assertEqual(zlib.decompress(payload),frames[-1])

    def test_ctrl_u_character_limits_and_password_masking(self):
        report=json.loads(self.run_feature("text-qol").stdout)
        self.assertTrue(all(report.values()))

    def test_setup_retries_invalid_names_without_discarding_other_fields(self):
        self.run_feature("settings-save","--name","Before")
        report=json.loads(self.run_feature("setup-retry-save").stdout)
        self.assertTrue(report["saved"]);self.assertTrue(report["display_error"]);self.assertTrue(report["device_error"])
        self.assertEqual(json.loads(self.run_feature("settings-read").stdout)["name"],"Stevie")

    def test_setup_cancel_and_home_preserve_previous_saved_settings(self):
        self.run_feature("settings-save","--name","Before")
        before={str(p.relative_to(self.card)):p.read_bytes() for p in self.card.rglob('*') if p.is_file()}
        for mode in ("setup-cancel-time","setup-retry-cancel","setup-home"):
            with self.subTest(mode=mode):
                report=json.loads(self.run_feature(mode).stdout);self.assertFalse(report["saved"])
                after={str(p.relative_to(self.card)):p.read_bytes() for p in self.card.rglob('*') if p.is_file()}
                self.assertEqual(after,before)

    def test_cancelled_first_setup_does_not_create_settings(self):
        report=json.loads(self.run_feature("setup-cancel-time").stdout)
        self.assertFalse(report["saved"]);self.assertEqual(list(self.card.iterdir()),[])

    def test_setup_save_failure_preserves_current_settings_and_unknown_partial_data(self):
        self.run_feature("settings-save","--name","Before")
        precious=self.write("POS/Users/settings.0.json.part/precious.txt",b"keep this partial")
        report=json.loads(self.run_feature("setup-save-fail").stdout)
        self.assertFalse(report["saved"])
        self.assertEqual(json.loads(self.run_feature("settings-read").stdout)["name"],"Before")
        self.assertEqual(precious.read_bytes(),b"keep this partial")

    def test_browser_page_counts_and_previous_next_controls(self):
        for n in range(25):self.write(f"file-{n:02}.txt",f"document {n}".encode())
        report=json.loads(self.run_feature("browse-pages").stdout)
        self.assertEqual(report["pages"],7);self.assertTrue(report["last_page"])
        self.assertEqual(report["final_page"],"Folder page 1/3")
        self.assertEqual(len(list(self.card.iterdir())),25)

    def test_browser_empty_folder_boundary_actions_and_preview_keep_current_page(self):
        empty=json.loads(self.run_feature("browse-empty").stdout)
        self.assertTrue(empty["empty"]);self.assertEqual(empty["pages"],1)
        self.assertEqual(empty["final_page"],"Folder page 1/1")
        for n in range(25):self.write(f"file-{n:02}.txt",b"preview this file")
        preview=json.loads(self.run_feature("browse-preview").stdout)
        self.assertEqual(preview["final_page"],"Folder page 2/3")

    def test_installer_phase_labels_and_keep_sd_completion_message(self):
        self.assertTrue(all(json.loads(self.run_feature("phase-labels").stdout).values()))

    def test_input_context_global_escape_and_100_lifecycles(self):
        result=json.loads(self.run_feature("core").stdout);self.assertEqual(result["resources_remaining"],0);self.assertTrue(result["global_home_intercepted"])

    def test_shared_ui_home_propagation_text_context_and_password_masking(self):
        report=json.loads(self.run_feature("ui").stdout);self.assertTrue(report["home_bubbles_to_launcher"]);self.assertFalse(report["password_display_leaked"])

    def test_settings_roundtrip_and_redundant_corruption_recovery(self):
        self.run_feature("settings-save","--name","Stevie")
        self.run_feature("settings-save","--name","Pan")
        latest=self.card/"POS/Users/settings.0.json";latest.write_bytes(b"torn write")
        read=json.loads(self.run_feature("settings-read").stdout);self.assertEqual(read["name"],"Stevie")
        self.run_feature("settings-save","--name","Recovered")
        self.assertEqual((self.card/"POS/Users/settings.0.json.corrupt-0").read_bytes(),b"torn write")
        self.assertEqual(json.loads(self.run_feature("settings-read").stdout)["name"],"Recovered")

    def test_unknown_settings_format_is_preserved(self):
        self.run_feature("settings-save")
        p=self.card/"POS/Users/settings.1.json";outer=json.loads(p.read_text());payload=json.loads(outer["payload"]);payload["format"]=99
        outer["payload"]=json.dumps(payload);outer["sha256"]=hashlib.sha256(outer["payload"].encode()).hexdigest();p.write_text(json.dumps(outer));before=p.read_bytes()
        self.run_feature("settings-read",expected=1);self.run_feature("settings-save",expected=1);self.assertEqual(p.read_bytes(),before)

    def test_deep_corrupt_settings_are_rejected_without_crash(self):
        self.write("POS/Users/settings.1.json",b"["*1000+b"0"+b"]"*1000)
        self.run_feature("settings-read",expected=1)

    def test_scan_and_standard_zip_include_user_data_only(self):
        self.seed();scan=json.loads(self.run_feature("scan").stdout);self.assertEqual(scan["files"],len(self.files));self.assertEqual(scan["bytes"],sum(map(len,self.files.values())))
        self.run_feature("backup");p=self.card/BACKUP
        with zipfile.ZipFile(p) as z:
            self.assertIsNone(z.testzip());self.assertEqual(set(z.namelist()),{"POS-Backup.json"}|{"files/"+name for name in self.files})
            for name,data in self.files.items():self.assertEqual(z.read("files/"+name),data)
            meta=json.loads(z.read("POS-Backup.json"));self.assertEqual(meta["source_bytes"],sum(map(len,self.files.values())))
        self.run_feature("verify");self.run_feature("match")
        self.assertEqual((self.card/(BACKUP+".sha256")).read_text(),hashlib.sha256(p.read_bytes()).hexdigest())

    def test_backup_cancel_never_marks_partial_complete(self):
        self.seed();self.run_feature("backup","--cancel","backup","--after",8192,expected=1)
        self.assertFalse((self.card/BACKUP).exists());self.assertTrue((self.card/(BACKUP+".part")).exists())
        for name,data in self.files.items():self.assertEqual((self.card/name).read_bytes(),data)
        self.run_feature("backup",expected=1)

    def test_power_interruption_preserves_originals_and_partial_archive(self):
        self.seed();self.run_feature("backup","--interrupt","backup",expected=99)
        self.assertFalse((self.card/BACKUP).exists())
        self.assertEqual((self.card/"Documents/Stevie.txt").read_bytes(),self.files["Documents/Stevie.txt"])

    def test_insufficient_backup_space_and_existing_archive_preserved(self):
        self.seed();self.run_feature("backup","--free",1,expected=1);self.assertFalse((self.card/(BACKUP+".part")).exists())
        self.run_feature("backup");before=(self.card/BACKUP).read_bytes();self.run_feature("backup",expected=1);self.assertEqual((self.card/BACKUP).read_bytes(),before)

    def test_symlink_and_oversize_are_not_silently_skipped(self):
        target=self.base/"external";target.write_text("outside");(self.card/"link").symlink_to(target)
        self.run_feature("backup",expected=1);(self.card/"link").unlink()
        with (self.card/"too-large").open("wb") as f:f.truncate(0x100000000)
        self.run_feature("scan",expected=1)

    def test_restore_is_streamed_idempotent_and_keeps_archive(self):
        self.seed();self.run_feature("backup");dest=self.destination();before=(self.card/BACKUP).read_bytes()
        self.run_feature("restore","--archive",self.card/BACKUP,card=dest)
        for name,data in self.files.items():self.assertEqual((dest/name).read_bytes(),data)
        again=json.loads(self.run_feature("restore","--archive",self.card/BACKUP,card=dest).stdout);self.assertEqual(again["conflicts"],0)
        self.assertEqual((self.card/BACKUP).read_bytes(),before);self.assertFalse((dest/"POS/System/core.bin").exists())

    def test_restore_conflicts_and_directory_conflicts_preserve_both(self):
        self.seed();self.run_feature("backup");dest=self.destination();(dest/"Documents").mkdir();(dest/"Documents/Stevie.txt").write_bytes(b"new document");(dest/"Music").write_bytes(b"a user file")
        result=json.loads(self.run_feature("restore","--archive",self.card/BACKUP,card=dest).stdout);self.assertGreaterEqual(result["conflicts"],2)
        self.assertEqual((dest/"Documents/Stevie.txt").read_bytes(),b"new document");self.assertEqual((dest/"Music").read_bytes(),b"a user file")
        recovered=list((dest/"POS/Recovered").rglob("Stevie.txt"));self.assertEqual(recovered[0].read_bytes(),self.files["Documents/Stevie.txt"])

    def test_cancelled_restore_can_resume(self):
        self.seed();self.run_feature("backup");dest=self.destination()
        self.run_feature("restore","--archive",self.card/BACKUP,"--cancel","restore","--after",8192,card=dest,expected=1)
        self.run_feature("restore","--archive",self.card/BACKUP,card=dest)
        for name,data in self.files.items():self.assertEqual((dest/name).read_bytes(),data)

    def test_corrupt_archive_prevents_any_restore_writes(self):
        self.seed();self.run_feature("backup");p=self.card/BACKUP
        with zipfile.ZipFile(p) as z:entry=z.getinfo("files/Documents/Stevie.txt");offset=entry.header_offset+30+len(entry.filename.encode())
        data=bytearray(p.read_bytes());data[offset]^=1;p.write_bytes(data);dest=self.destination()
        self.run_feature("restore","--archive",p,card=dest,expected=1);self.assertEqual(list(dest.iterdir()),[])

    def test_invalid_paths_collision_and_system_restore_are_rejected(self):
        bad_sets=[[("files/../escape",b"x")],[("files/POS/System/core",b"x")],[("files/POS.previous/user",b"x")],[("files//absolute",b"x")],
                  [("files/Documents/A",b"x"),("files/documents/a",b"y")],[("files/Documents/a",b"x"),("files/Documents/a/b",b"y")]]
        dest=self.destination();p=self.card/BACKUP
        for files in bad_sets:
            with self.subTest(files=files):
                p.write_bytes(archive_bytes(files));self.run_feature("restore","--archive",p,card=dest,expected=1);self.assertEqual(list(dest.iterdir()),[])

    def test_unknown_backup_format_and_migration_are_preserved(self):
        for field in ("format","migration"):
            p=self.write(BACKUP,archive_bytes([("files/Documents/a",b"x")],{field:99}));dest=self.base/field;dest.mkdir();before=p.read_bytes()
            self.run_feature("restore","--archive",p,card=dest,expected=1);self.assertEqual(list(dest.iterdir()),[]);self.assertEqual(p.read_bytes(),before)

    def test_future_preferences_are_preserved_in_recovered(self):
        outer={"payload":json.dumps({"format":99}),"sha256":"not used in format screening"}
        p=self.write(BACKUP,archive_bytes([("files/POS/Users/settings.1.json",json.dumps(outer).encode())]));dest=self.destination()
        self.run_feature("restore","--archive",p,card=dest);self.assertFalse((dest/"POS/Users/settings.1.json").exists())
        self.assertTrue(list((dest/"POS/Recovered").rglob("settings.1.json")))

    def test_backup_matches_detects_changed_source(self):
        self.seed();self.run_feature("backup");self.write("Documents/Stevie.txt",b"changed");self.run_feature("match",expected=1)

    def test_legacy_preferences_migrate_and_original_remains_accessible(self):
        legacy={"format":0,"user_name":"Stevie","device_name":"PANPUTER","timezone":"UTC0","brightness":25}
        p=self.write(BACKUP,archive_bytes([("files/POS/Users/settings.json",json.dumps(legacy).encode())]));dest=self.destination()
        self.run_feature("restore","--archive",p,card=dest)
        settings=json.loads(self.run_feature("settings-read",card=dest).stdout)
        self.assertEqual(settings["name"],"Stevie");self.assertEqual(settings["brightness"],25)
        originals=list((dest/"POS/Recovered").rglob("settings.json"));self.assertEqual(json.loads(originals[0].read_text()),legacy)

    def test_restore_cannot_follow_existing_directory_symlink(self):
        p=self.write(BACKUP,archive_bytes([("files/Documents/a.txt",b"archive document")]))
        dest=self.destination();outside=self.base/"outside";outside.mkdir();(dest/"Documents").symlink_to(outside,target_is_directory=True)
        self.run_feature("restore","--archive",p,card=dest)
        self.assertEqual(list(outside.iterdir()),[])
        self.assertTrue(list((dest/"POS/Recovered").rglob("a.txt")))

    def package(self,version,release):
        folder=self.base/("package-"+version);firmware=self.base/(version+".bin");firmware.write_bytes(image(version=version))
        payload=self.base/("payload-"+version);(payload/"System").mkdir(parents=True);(payload/"System/release.txt").write_text(release)
        posctl.make_package(firmware,payload,folder);return folder/"pos-install"

    def installed(self):
        package=self.package("0.1.0-dev","old system")
        shutil.copytree(package,self.card/"pos-install");self.run_feature("install");self.run_feature("boot")
        self.write("POS/Users/Owner/Documents/Stevie.txt",b"the original document")
        self.write("Music/song.dat",b"offline music")
        return package

    def test_repair_reinstates_only_package_system_files(self):
        package=self.installed();shutil.copytree(package,self.card/"pos-install")
        self.write("POS/System/release.txt",b"corrupt sector")
        self.run_feature("repair");self.run_feature("boot")
        self.assertEqual((self.card/"POS/System/release.txt").read_text(),"old system")
        self.assertEqual((self.card/"POS/Users/Owner/Documents/Stevie.txt").read_bytes(),b"the original document")

    def test_wrong_package_cannot_repair_confirmed_system(self):
        self.installed();package=self.package("0.2.0-dev","new system");shutil.copytree(package,self.card/"pos-install")
        marker=(self.card/"POS/System/.boot-ok").read_bytes();self.run_feature("repair",expected=1)
        self.assertEqual((self.card/"POS/System/.boot-ok").read_bytes(),marker)
        self.assertEqual((self.card/"POS/System/release.txt").read_text(),"old system")

    def test_reinstall_resumes_after_retirement_and_restores_user_data(self):
        self.installed();self.run_feature("backup");backup=(self.card/BACKUP).read_bytes()
        package=self.package("0.2.0-dev","new system");shutil.copytree(package,self.card/"pos-install")
        self.run_feature("reinstall","--interrupt","retired",expected=99)
        self.assertFalse((self.card/"POS").exists());self.assertTrue((self.card/"POS.previous/System/release.txt").exists())
        self.run_feature("reinstall");self.run_feature("boot");self.run_feature("restore")
        self.assertEqual((self.card/"POS/System/release.txt").read_text(),"new system")
        self.assertEqual((self.card/"POS/Users/Owner/Documents/Stevie.txt").read_bytes(),b"the original document")
        self.assertEqual((self.card/"Music/song.dat").read_bytes(),b"offline music")
        self.assertEqual((self.card/"POS.previous/System/release.txt").read_text(),"old system")
        self.assertEqual((self.card/BACKUP).read_bytes(),backup)

    def test_stale_or_corrupt_backup_cannot_retire_old_system(self):
        self.installed();self.run_feature("backup");self.write("POS/Users/new-file.txt",b"not backed up")
        package=self.package("0.2.0-dev","new system");shutil.copytree(package,self.card/"pos-install")
        self.run_feature("reinstall",expected=1)
        self.assertFalse((self.card/"POS.previous").exists());self.assertFalse((self.card/"pos-reinstall.pos").exists())
        self.assertEqual((self.card/"POS/System/release.txt").read_text(),"old system")

    def test_unknown_journal_and_previous_tree_are_preserved(self):
        self.installed();self.run_feature("backup");package=self.package("0.2.0-dev","new system");shutil.copytree(package,self.card/"pos-install")
        self.write("pos-reinstall.pos",b"partial journal")
        self.run_feature("reinstall",expected=1);self.assertEqual((self.card/"pos-reinstall.pos").read_bytes(),b"partial journal")
        (self.card/"pos-reinstall.pos").unlink();self.write("POS.previous/precious.txt",b"only original")
        self.run_feature("reinstall",expected=1);self.assertEqual((self.card/"POS.previous/precious.txt").read_bytes(),b"only original")

    def test_future_or_non_json_reinstall_journal_cannot_retire_user_data(self):
        self.installed();self.run_feature("backup");package=self.package("0.2.0-dev","new system");shutil.copytree(package,self.card/"pos-install")
        journal={"format":99,"package":hashlib.sha256((package/"manifest.pos").read_bytes()).hexdigest(),"backup":hashlib.sha256((self.card/BACKUP).read_bytes()).hexdigest()}
        for version,suffix in ((99,""),(1,"non-json-tail")):
            with self.subTest(version=version,suffix=suffix):
                journal["format"]=version;text=json.dumps(journal)+suffix
                self.write("pos-reinstall.pos",text.encode());self.run_feature("reinstall",expected=1)
                self.assertFalse((self.card/"POS.previous").exists());self.assertEqual((self.card/"pos-reinstall.pos").read_text(),text)


    def test_recorder_home_finalizes_wav_and_releases_microphone(self):
        self.run_feature('recorder-ui-home')
        import wave
        with wave.open(str(self.card/'Recordings/Recording-00001.wav'),'rb') as wav:
            self.assertEqual((wav.getnframes(),wav.getframerate(),wav.getnchannels()),(256,16000,1))
    def test_files_audio_player_stops_at_eof_without_entering_library_loop(self):
        import wave
        file=self.card/'Music/tone.wav';file.parent.mkdir()
        with wave.open(str(file),'wb') as wav:
            wav.setparams((1,2,22050,0,'NONE','not compressed'));wav.writeframes(struct.pack('<2048h',*[i%1000 for i in range(2048)]))
        self.run_feature('audio-ui-file-eof')

    def test_recorder_saves_buffered_tail_on_home_and_creates_header_before_capture(self):
        self.run_feature('recorder-ui-tail')
        import wave
        with wave.open(str(self.card/'Recordings/Recording-00001.wav'), 'rb') as wav:
            self.assertEqual(wav.getnframes(), 384)
            samples = struct.unpack('<384h', wav.readframes(384))
            self.assertEqual(samples[256:], (1234,) * 128)

    def test_recorder_reports_true_zero_input_without_faking_a_signal(self):
        self.run_feature('recorder-ui-silent')
        import wave
        with wave.open(str(self.card/'Recordings/Recording-00001.wav'), 'rb') as wav:
            self.assertEqual(wav.getnframes(), 200 * 256)
            self.assertEqual(set(wav.readframes(wav.getnframes())), {0})

    def test_display_off_wakes_in_same_system_menu_and_consumes_enter_or_home(self):
        for key in ('enter', 'home'):
            with self.subTest(key=key):
                self.run_feature('settings-ui-display-' + key)


if __name__=="__main__":unittest.main()
