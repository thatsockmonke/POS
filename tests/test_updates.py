import copy
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import pos_updates
from test_installer import posctl

class UpdateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='pos-updates-build-');cls.runner=Path(cls.build.name)/'updates'
        sdk=Path(os.environ.get('IDF_PATH','/workspace/pos-cloud/esp-idf-v5.5.5'));cjson=sdk/'components/json/cJSON'
        sources=[ROOT/'tests/update_runner.c',*sorted((ROOT/'components/pos_core').glob('*.c')),*sorted((ROOT/'components/pos_install').glob('*.c')),*sorted((ROOT/'components/pos_update').glob('*.c')),cjson/'cJSON.c']
        cmd=['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all']
        for c in ('pos_core','pos_install','pos_update'):cmd+=['-I',str(ROOT/'components'/c/'include')]
        subprocess.run(cmd+['-I',str(cjson),*map(str,sources),'-lcrypto','-lm','-o',str(cls.runner)],check=True)
        cls.ui_runner=Path(cls.build.name)/'update-ui'
        ui_sources=[ROOT/'tests/update_ui_runner.c', ROOT/'main/updates.c',
                    ROOT/'components/pos_device/pos_update_service.c', *sources[1:]]
        subprocess.run(cmd+['-I',str(ROOT/'tests/stubs'),'-I',str(ROOT/'main'),
                      '-I',str(ROOT/'components/pos_device/include'),'-I',str(cjson),
                      *map(str,ui_sources),'-lcrypto','-lm','-o',str(cls.ui_runner)],check=True)

    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='pos-updates-test-');self.addCleanup(self.temp.cleanup);self.base=Path(self.temp.name);self.card=self.base/'card';self.card.mkdir();self.feed=self.base/'feed';self.feed.mkdir()
        self.private=self.base/'private.pem';self.public=self.base/'public.pem'
        pos_updates.keygen(self.private,self.public)
        self.old={'format':1,'model':'m5stack-cardputer-adv','version':'0.4.0-dev','firmware':self.record('firmware.bin',b'f'*512),'files':[self.record('System/Assets/icons.bin',b'old icons'),self.record('System/README.txt',b'old README')]}
        self.write('POS/System/Assets/icons.bin',b'old icons');self.write('POS/System/README.txt',b'old README');self.write('POS/Users/Owner/Documents/precious.txt',b'keep my words')
        raw=json.dumps(self.old,separators=(',',':')).encode();self.write('POS/System/installed.pos',raw);self.write('POS/.pos-transaction',hashlib.sha256(raw).hexdigest().encode())
        self.meta=self.base/'manifest.json'
    @staticmethod
    def record(path,data):return dict(path=path,size=len(data),sha256=hashlib.sha256(data).hexdigest())
    def write(self,relative,data):
        p=self.card/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);return p
    def run_c(self,mode,*args,expected=0):
        r=subprocess.run([str(self.runner),mode,*map(str,args)],capture_output=True,text=True)
        self.assertEqual(r.returncode,expected,r.stdout+r.stderr);return r
    def signed(self,kind='assets',sequence=1,minimum=None,mutate=None):
        package=copy.deepcopy(self.old);package['files'][0]=self.record('System/Assets/icons.bin',b'new icons');(self.feed/'payload/System/Assets').mkdir(parents=True,exist_ok=True);(self.feed/'payload/System/Assets/icons.bin').write_bytes(b'new icons')
        if kind=='firmware':package['version']='0.4.1-dev';package['firmware']=self.record('firmware.bin',b'n'*512);(self.feed/'firmware.bin').write_bytes(b'n'*512)
        p=dict(format=1,model=self.old['model'],release='0.4.1-dev',sequence=sequence,kind=kind,base_url='https://updates.example.test/pos/',package=package)
        if minimum is not None:p['minimum_version']=minimum
        if mutate:mutate(p)
        body=json.dumps(p,separators=(',',':'));sig=subprocess.run(['openssl','dgst','-sha256','-sign',str(self.private)],input=body.encode(),check=True,capture_output=True).stdout
        self.meta.write_text(json.dumps(dict(payload=body,signature=sig.hex())));return p
    def load(self,expected=0,current='0.4.0-dev',floor=0):return self.run_c('meta',self.card,self.meta,self.public,current,floor,expected=expected)
    def apply(self,expected=0,interrupt='',failure='',space=1<<40):return self.run_c('apply',self.card,self.meta,self.public,'0.4.0-dev',0,self.feed,interrupt,failure,space,expected=expected)
    def unchanged(self):
        self.assertEqual((self.card/'POS/System/Assets/icons.bin').read_bytes(),b'old icons');self.assertEqual((self.card/'POS/Users/Owner/Documents/precious.txt').read_bytes(),b'keep my words')
    def ui(self,mode):
        result=subprocess.run([str(self.ui_runner),mode,str(self.card),str(self.meta),str(self.public)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return result
    def test_builtin_feed_defaults_preserve_custom_sources_sequence_and_nvs_errors(self):
        expected=subprocess.run(['openssl','pkey','-pubin','-in',str(ROOT/'updates/public-key.pem'),'-outform','DER'],check=True,capture_output=True).stdout
        self.assertEqual(self.ui('source-defaults').stdout.strip(),hashlib.sha256(expected).hexdigest())
        self.ui('nvs-error')
    def test_update_ui_verifies_signature_and_requests_install_confirmation(self):
        self.signed('firmware');self.ui('install')
    def test_update_ui_cancel_never_calls_flash_or_changes_current_system(self):
        self.signed('firmware');self.ui('cancel');self.unchanged()
    def test_update_ui_bad_signature_never_reaches_installer(self):
        self.signed('firmware');self.meta.write_text(self.meta.read_text().replace('0.4.1-dev','0.4.2-dev'));self.ui('bad-signature');self.unchanged()
    def test_update_ui_same_version_reports_up_to_date_without_installing(self):
        self.signed('firmware',mutate=lambda p:(p.update(release='0.4.0-dev'),p['package'].update(version='0.4.0-dev')));self.ui('current')
    def test_update_ui_opens_wifi_settings_before_checking_when_offline(self):
        self.signed('firmware');self.ui('wifi')
    def test_update_ui_synchronizes_missing_clock_before_https(self):
        self.signed('firmware');self.ui('sync-time')
    def test_update_ui_time_sync_failure_does_not_download_or_install(self):
        self.signed('firmware');self.ui('time-failed')
    def test_failed_boot_policy_safe_mode_and_manual_exit(self):
        self.run_c('boot-policy',self.card)
    def test_crash_records_are_readable_persistent_never_overwritten_and_fail_safely(self):
        first=Path(self.run_c('crash',self.card).stdout.strip());before=first.read_bytes();second=Path(self.run_c('crash',self.card).stdout.strip())
        self.assertNotEqual(first,second);self.assertEqual(first.read_bytes(),before)
        data=json.loads(before);self.assertEqual(data['pc'],0x42001234);self.assertEqual(data['panic_version'],'0.3.9-dev');self.assertTrue(data['panic_snapshot'])
        empty=self.base/'emptycard';empty.mkdir();fresh=Path(self.run_c('crash',empty).stdout.strip());self.assertEqual(fresh.parent,empty/'CrashReports');self.assertFalse((empty/'POS').exists())
        root=self.base/'badcard';root.mkdir();(root/'POS').write_bytes(b'preserve')
        self.run_c('crash',root,expected=1);self.assertEqual((root/'POS').read_bytes(),b'preserve')
    def test_battery_and_selection_overlay_render_above_every_screen(self):
        self.run_c('overlay',self.card)
    def test_p256_fingerprint_matches_independent_der_and_rejects_other_curves(self):
        der=subprocess.run(['openssl','pkey','-pubin','-in',str(self.public),'-outform','DER'],capture_output=True,check=True).stdout
        self.assertEqual(self.run_c('fingerprint',self.public).stdout.strip(),hashlib.sha256(der).hexdigest())
        bad=self.base/'other.pem';subprocess.run(['openssl','ecparam','-name','secp384r1','-genkey','-out',str(bad)],check=True,capture_output=True)
        pub=subprocess.run(['openssl','pkey','-in',str(bad),'-pubout'],capture_output=True,check=True).stdout;bad.write_bytes(pub);self.run_c('fingerprint',bad,expected=1)
    def test_signature_tampering_untrusted_key_and_bad_formats_are_rejected(self):
        self.signed();self.load();j=json.loads(self.meta.read_text());j['payload']=j['payload'].replace('0.4.1-dev','0.4.2-dev');self.meta.write_text(json.dumps(j));self.load(expected=1)
        self.signed();other=self.base/'other-public.pem';pos_updates.keygen(self.base/'other-private.pem',other);self.run_c('meta',self.card,self.meta,other,'0.4.0-dev',0,expected=1)
        for change in [lambda p:p.update(format=99),lambda p:p.update(model='wrong'),lambda p:p['package']['files'][0].update(path='../user.txt'),lambda p:p.update(base_url='http://plain.test/'),lambda p:p['package']['files'].append(copy.deepcopy(p['package']['files'][0]))]:self.signed(mutate=change);self.load(expected=1)
    def test_signed_sequence_and_firmware_downgrade_checks(self):
        self.signed(sequence=8);self.load();self.load(expected=1,floor=8);self.load(expected=1,floor=9)
        self.signed('firmware');self.load(current='0.4.1-dev',expected=1);self.load(current='0.5.0',expected=1)
        self.signed(mutate=lambda p:p['package'].update(version='0.3.8-dev'));self.load(expected=1)
    def test_update_requires_exactly_current_fields_without_minimum(self):
        self.signed('firmware');self.load(current='0.3.0-dev')
        self.signed(minimum='0.0.0');self.load(expected=1)
        self.signed(minimum='99.0.0');self.load(expected=1)

    def test_malformed_legacy_metadata_unknown_fields_and_bad_current_version_are_rejected(self):
        for value in (None,42,{},'bad','0.04.0','1'*200):
            self.signed(mutate=lambda p:p.update(minimum_version=value));self.load(expected=1)
        self.signed(mutate=lambda p:p.update(unexpected=True));self.load(expected=1)
        self.signed('firmware');self.load(current='bad',expected=1)

    def test_asset_only_update_never_flashes_and_keeps_user_data_and_previous_system(self):
        self.signed();r=self.apply();self.assertEqual(r.stdout.strip(),'0 0');self.assertEqual((self.card/'POS/System/Assets/icons.bin').read_bytes(),b'new icons');self.assertEqual((self.card/'POS/System/README.txt').read_bytes(),b'old README')
        self.assertEqual(self.run_c('recover',self.card,'0.4.0-dev').stdout.strip(),'1');self.assertEqual(self.run_c('confirm',self.card).stdout.strip(),'1');self.assertFalse((self.card/'pos-update.pos').exists());self.assertEqual(next((self.card/'POS/Updates').glob('*/old-System/Assets/icons.bin')).read_bytes(),b'old icons');self.assertEqual((self.card/'POS/Users/Owner/Documents/precious.txt').read_bytes(),b'keep my words')
    def test_asset_firmware_hash_mismatch_is_refused_before_changes(self):
        self.signed(mutate=lambda p:p['package']['firmware'].update(sha256='0'*64));self.apply(expected=1);self.unchanged();self.assertFalse((self.card/'pos-update.pos').exists())
    def test_cancelled_preparation_can_retry_without_replacing_current_data(self):
        self.signed();self.apply(expected=1,failure='cancel');self.unchanged();self.assertFalse((self.card/'pos-update.pos').exists())
        self.apply();self.assertEqual((self.card/'POS/System/Assets/icons.bin').read_bytes(),b'new icons')

    def test_download_hash_failure_low_space_and_write_failure_leave_current_system(self):
        self.signed();(self.feed/'payload/System/Assets/icons.bin').write_bytes(b'corrupt');self.apply(expected=1);self.unchanged();self.assertFalse((self.card/'pos-update.pos').exists())
        self.signed();self.apply(expected=1,space=1);self.unchanged()
        self.signed('firmware');self.apply(expected=1,failure='write');self.unchanged();self.assertFalse((self.card/'pos-update.pos').exists())
    def test_power_loss_at_each_system_swap_boundary_recovers_previous_asset_system(self):
        for phase in ('update-journaled','update-retired','update-swapped'):
            with self.subTest(phase=phase):
                self.signed(sequence=abs(hash(phase))%100000+1);self.apply(expected=99,interrupt=phase);self.run_c('recover',self.card,'0.4.0-dev');self.unchanged()
    def test_firmware_update_selects_only_after_verified_sd_and_supports_rollback(self):
        self.signed('firmware');self.assertEqual(self.apply().stdout.strip(),'1 1');self.assertEqual(self.run_c('recover',self.card,'0.4.1-dev').stdout.strip(),'1');self.run_c('recover',self.card,'0.4.0-dev');self.unchanged();self.assertTrue(list((self.card/'POS/Updates').glob('*/failed-System')))
    def test_pending_new_firmware_with_damaged_system_requests_ota_rollback(self):
        self.signed('firmware');self.apply();(self.card/'POS/System/Assets/icons.bin').write_bytes(b'bad');self.assertEqual(self.run_c('recover',self.card,'0.4.1-dev').stdout.strip(),'2');self.unchanged()
    def test_failed_boot_selection_restores_system_and_preserves_staging(self):
        self.signed('firmware');self.apply(expected=1,failure='select');self.unchanged();self.assertFalse((self.card/'pos-update.pos').exists())
    def test_unknown_or_torn_journal_is_never_deleted(self):
        p=self.write('pos-update.pos',b'{torn');self.run_c('recover',self.card,'0.4.0-dev',expected=1);self.assertEqual(p.read_bytes(),b'{torn');self.unchanged()
    def test_version_order_matches_semver_numeric_and_prerelease_rules(self):
        values=['0.3.9','0.3.10-dev','0.3.10-dev.2','0.3.10-dev.11','0.3.10','0.4.0','1.0.0']
        for a,b in zip(values,values[1:]):self.assertEqual(self.run_c('version',a,b).stdout.strip(),'-1');self.assertLess(pos_updates.compare(a,b),0)
        for bad in ['0.04.0','0.4','0.4.0-01','0.4.0-','hello','0.4.0-a..b']:self.run_c('version',bad,'0.4.0',expected=1)
    def test_timezone_presets_custom_rules_and_real_calendar_validation(self):
        for rule in ['UTC0','PST8PDT,M3.2.0,M11.1.0','IST-5:30','NZST-12NZDT,M9.5.0,M4.1.0/3']:self.run_c('timezone',rule)
        for bad in ['America/Los_Angeles','UTC99','PST8PDT,M13.1.0,M11.1.0','UTC0junk,','UTC0BAD,M3.2.0','UTC0BAD,M3.2.0,M11.1.8']:self.run_c('timezone',bad,expected=1)
        for text in ['2024-02-29 13:45','2026-10-16 00:00','2099-12-31 23:59']:
            expected=int(datetime.datetime.strptime(text,'%Y-%m-%d %H:%M').replace(tzinfo=datetime.timezone.utc).timestamp());self.assertEqual(int(self.run_c('utc',text).stdout),expected)
        for bad in ['2023-02-29 00:00','2026-04-31 00:00','2026-10-16 24:00','2026-10-16 13:60','1900-01-01 00:00','2026-1x-16 00:00']:self.run_c('utc',bad,expected=1)
    def test_battery_smoothing_hysteresis_full_delay_and_unavailable_state(self):
        readings=[(1,3790)]+[(i*5000,3790+(i%2)*4) for i in range(1,20)]+[(100000+i*5000,4200) for i in range(30)]+[(260000+i*5000,3950) for i in range(20)]+[(400000,0)]
        p=self.base/'samples.txt';p.write_text(''.join(f'{t} {mv}\n' for t,mv in readings));rows=[list(map(int,line.split())) for line in self.run_c('battery',p).stdout.splitlines()]
        self.assertEqual({r[2] for r in rows[:20]},{40});self.assertEqual({r[3] for r in rows[:20]},{2});self.assertFalse(rows[21][-1]);self.assertTrue(rows[49][-1]);self.assertEqual(rows[49][2],100);self.assertFalse(rows[-2][-1]);self.assertEqual(rows[-1][-2:], [0,0])
    def test_unknown_length_http_response_is_bounded_before_publication(self):
        source=self.base/'large-response';source.write_bytes(b'x'*4097)
        self.run_c('limit',self.card,source,4096,expected=1)
        self.assertFalse((self.card/'download.bin').exists());self.assertEqual(source.read_bytes(),b'x'*4097)
    def test_symlinked_system_directory_is_refused_without_mutating_target(self):
        self.signed();system=self.card/'POS/System';original=self.base/'external-System';system.rename(original);system.symlink_to(original,target_is_directory=True)
        self.apply(expected=1);self.assertEqual((original/'Assets/icons.bin').read_bytes(),b'old icons');self.assertTrue(system.is_symlink())

    def test_signing_tool_round_trip_and_no_key_or_package_overwrite(self):
        installed=self.base/'installed.pos';installed.write_text(json.dumps(self.old));payload=self.base/'assets';(payload/'System/Assets').mkdir(parents=True);(payload/'System/Assets/icons.bin').write_bytes(b'tool icons')
        args=SimpleNamespace(kind='assets',release='0.4.1-dev',sequence=7,base_url='https://updates.example.test/pos/',output=self.base/'signed',payload=payload,installed_manifest=installed,private_key=self.private,firmware=None)
        out=pos_updates.make_update(args,posctl);self.meta=out/'manifest.json';self.assertNotIn('minimum_version',json.loads(json.loads(self.meta.read_text())['payload']));self.load();self.assertEqual((out/'payload/System/Assets/icons.bin').read_bytes(),b'tool icons')
        with self.assertRaises(ValueError):pos_updates.make_update(args,posctl)
        old=self.private.read_bytes()
        with self.assertRaises(ValueError):pos_updates.keygen(self.private,self.public)
        self.assertEqual(self.private.read_bytes(),old);self.assertEqual(self.private.stat().st_mode&0o777,0o600)

    def history_seed(self, version='0.6.1-dev', sequence=10, variant=''):
        package=copy.deepcopy(self.old);package['version']=version
        firmware=(version+' firmware').encode()*64
        package['firmware']=self.record('firmware.bin',firmware)
        for i,record in enumerate(package['files']):
            content=(version+str(i)+variant).encode();package['files'][i]=self.record(record['path'],content)
            self.write('POS/'+record['path'],content)
        raw=json.dumps(package,separators=(',',':')).encode()
        self.write('POS/System/installed.pos',raw);self.write('POS/.pos-transaction',hashlib.sha256(raw).hexdigest().encode())
        body=json.dumps(dict(format=1,model=package['model'],release=version,sequence=sequence,kind='firmware',base_url='https://history.test/releases/'+version+'/',package=package),separators=(',',':'))
        signature=subprocess.run(['openssl','dgst','-sha256','-sign',str(self.private)],input=body.encode(),check=True,capture_output=True).stdout
        meta=self.base/(version+'.json');meta.write_text(json.dumps(dict(payload=body,signature=signature.hex())))
        binary=self.base/(version+'.bin');binary.write_bytes(firmware)
        return meta,binary,hashlib.sha256(raw).hexdigest()
    def history_capture(self, meta, binary, expected=0, failure='', pin=''):
        return self.run_c('history-capture',self.card,meta,self.public,binary,failure,'',pin,expected=expected)
    def test_history_captures_verified_pair_and_rolls_back_through_existing_transaction(self):
        meta,binary,identity=self.history_seed();self.history_capture(meta,binary)
        archive=self.card/'POS/.History'/identity
        self.assertEqual((archive/'firmware.bin').read_bytes(),binary.read_bytes())
        self.assertTrue((archive/'complete').is_file())
        self.history_seed('0.6.2-dev',11)
        result=self.run_c('history-rollback',self.card,identity,self.public,'-',expected=0)
        self.assertEqual(result.stdout.strip(),'1 1')
        self.assertEqual(self.run_c('recover',self.card,'0.6.1-dev').stdout.strip(),'1')
        self.run_c('confirm',self.card)
        self.assertEqual(json.loads((self.card/'POS/System/installed.pos').read_bytes())['version'],'0.6.1-dev')
        self.assertEqual((self.card/'POS/Users/Owner/Documents/precious.txt').read_bytes(),b'keep my words')
    def test_history_cancellation_and_bad_flash_never_mark_complete_or_change_current_system(self):
        meta,binary,identity=self.history_seed();before=(self.card/'POS/System/installed.pos').read_bytes()
        self.history_capture(meta,binary,expected=1,failure='cancel')
        self.assertFalse((self.card/'POS/.History'/identity/'complete').exists())
        self.assertEqual((self.card/'POS/System/installed.pos').read_bytes(),before)
        binary.write_bytes(b'bad image');self.history_capture(meta,binary,expected=1)
        self.assertFalse((self.card/'POS/.History'/identity).exists())
    def test_history_refuses_corrupt_signature_resources_or_firmware_before_flash(self):
        meta,binary,identity=self.history_seed();self.history_capture(meta,binary)
        archive=self.card/'POS/.History'/identity;self.history_seed('0.6.2-dev',11)
        for path in [archive/'firmware.bin',archive/'payload/System/README.txt',archive/'manifest.json']:
            original=path.read_bytes();path.write_bytes(original+b'corrupt')
            result=self.run_c('history-rollback',self.card,identity,self.public,'-',expected=1)
            self.assertEqual(result.stdout.strip(),'0 0');path.write_bytes(original)
    def test_history_keeps_fifteen_complete_pairs_and_pins_selected_rollback_target(self):
        first=None
        for i in range(17):
            meta,binary,identity=self.history_seed(f'0.6.{i+1}-dev',i+10)
            if first is None:first=identity
            self.history_capture(meta,binary,pin=first)
        rows=self.run_c('history-list',self.card,self.public).stdout.splitlines()
        self.assertEqual(len(rows),15)
        self.assertIn(first,[r.split()[0] for r in rows])
        self.assertTrue((self.card/'POS/.History'/identity/'complete').is_file())
    def test_history_rejects_symlink_and_unknown_entries_and_preserves_them(self):
        meta,binary,identity=self.history_seed();self.history_capture(meta,binary)
        archive=self.card/'POS/.History'/identity;outside=self.base/'precious';outside.write_text('keep')
        target=archive/'firmware.bin';target.unlink();target.symlink_to(outside)
        self.history_seed('0.6.2-dev',11)
        self.run_c('history-rollback',self.card,identity,self.public,'-',expected=1)
        self.assertEqual(outside.read_text(),'keep')
        unknown=self.card/'POS/.History/unknown';unknown.mkdir();(unknown/'keep').write_text('keep')
        self.run_c('history-list',self.card,self.public)
        self.assertEqual((unknown/'keep').read_text(),'keep')
    def test_history_failed_boot_selection_and_power_loss_restore_matching_old_system(self):
        meta,binary,identity=self.history_seed();self.history_capture(meta,binary)
        for failure,interrupt,expected in [('select','',1),('', 'update-swapped',99)]:
            self.history_seed('0.6.2-dev',11)
            self.run_c('history-rollback',self.card,identity,self.public,'-',failure,interrupt,expected=expected)
            self.run_c('recover',self.card,'0.6.2-dev')
            self.assertEqual(json.loads((self.card/'POS/System/installed.pos').read_bytes())['version'],'0.6.2-dev')

    def test_history_import_archives_retained_old_system_without_touching_current(self):
        meta,binary,identity=self.history_seed()
        import shutil
        retained=self.card/'POS/Updates/import/old-System';retained.parent.mkdir(parents=True)
        shutil.copytree(self.card/'POS/System',retained)
        self.history_seed('0.6.2-dev',11)
        before=(self.card/'POS/System/installed.pos').read_bytes()
        self.run_c('history-import',self.card,meta,self.public,binary,'','','',retained)
        self.assertTrue((self.card/'POS/.History'/identity/'complete').exists())
        self.assertEqual((self.card/'POS/System/installed.pos').read_bytes(),before)
    def test_same_resident_image_history_rolls_back_only_system_without_flash(self):
        meta,binary,identity=self.history_seed();self.history_capture(meta,binary)
        self.history_seed('0.6.1-dev',11,variant='updated assets')
        result=self.run_c('history-rollback',self.card,identity,self.public,'-')
        self.assertEqual(result.stdout.strip(),'0 0')
        self.assertEqual(self.run_c('recover',self.card,'0.6.1-dev').stdout.strip(),'1')
        self.run_c('confirm',self.card)
    def test_different_same_version_firmware_is_refused_as_ambiguous(self):
        meta,binary,identity=self.history_seed();self.history_capture(meta,binary)
        self.history_seed('0.6.1-dev',11,variant='updated assets')
        manifest=self.card/'POS/System/installed.pos';p=json.loads(manifest.read_bytes())
        p['firmware']=self.record('firmware.bin',b'different resident image')
        raw=json.dumps(p,separators=(',',':')).encode();manifest.write_bytes(raw)
        self.write('POS/.pos-transaction',hashlib.sha256(raw).hexdigest().encode())
        result=self.run_c('history-rollback',self.card,identity,self.public,'-',expected=1)
        self.assertEqual(result.stdout.strip(),'0 0')
