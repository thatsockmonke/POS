"""The real release-to-OTA publisher: signing, integrity, retries and version policy."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch
from test_installer import image
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import pos_release_updates as publisher
import pos_updates


class ReleaseUpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='pos-update-release-');self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name);self.root=self.base/'repo';(self.root/'updates').mkdir(parents=True)
        self.private=self.base/'private.pem';self.public=self.root/'updates/public-key.pem'
        fingerprint=pos_updates.keygen(self.private,self.public)
        self.config=dict(format=1,repository='owner/POS',branch='main',manifest='updates/latest.json',fingerprint=fingerprint)
        (self.root/'updates/publisher.json').write_text(json.dumps(self.config))
    def bundle(self,version='0.5.3-dev',data=b'System asset'):
        fw=bytes(image(version));rec=lambda p,b:dict(path=p,size=len(b),sha256=hashlib.sha256(b).hexdigest())
        manifest=dict(format=1,model='m5stack-cardputer-adv',version=version,firmware=rec('firmware.bin',fw),files=[rec('System/README.txt',data)])
        files={'card/pos-install/manifest.pos':json.dumps(manifest).encode(),'card/pos-install/firmware.bin':fw,
            'card/pos-install/payload/System/README.txt':data,'pos-installer-usb.bin':bytes(0x10000)+bytes(image(version,'pos_installer'))}
        files.update({name: ('Test distribution license: '+name).encode() for name in publisher.LICENSE_FILES})
        files['SHA256SUMS']=''.join(hashlib.sha256(b).hexdigest()+'  '+p+'\n' for p,b in files.items()).encode()
        out=self.base/f'pos-{version}.zip'
        with zipfile.ZipFile(out,'w',compression=zipfile.ZIP_DEFLATED) as z:
            for name,body in files.items():z.writestr(name,body)
        Path(str(out)+'.sha256').write_text(hashlib.sha256(out.read_bytes()).hexdigest()+'  '+out.name+'\n')
        return out
    def test_exact_release_is_signed_and_channel_verifies_with_pinned_key(self):
        result=publisher.publish(self.root,self.bundle(),self.private)
        self.assertEqual(result['sequence'],1);self.assertEqual(result['release'],'0.5.3-dev')
        self.assertEqual(result['base_url'],'https://raw.githubusercontent.com/owner/POS/main/updates/releases/0.5.3-dev/')
        self.assertEqual(publisher.validate_feed(self.root),result)
    def test_publisher_has_no_version_cutoff_and_preserves_legacy_reader_field(self):
        result=publisher.publish(self.root,self.bundle('0.1.0-dev'),self.private)
        self.assertEqual(result['minimum_version'],'0.0.0')
        self.assertNotIn('minimum_version',publisher.configuration(self.root))

    def test_transition_pins_old_feed_and_future_releases_drop_legacy_field(self):
        bridge=publisher.publish(self.root,self.bundle('0.6.0-dev'),self.private)
        old_feed=self.root/'updates/manifest.json';pinned=old_feed.read_bytes()
        self.assertEqual(bridge['minimum_version'],'0.0.0')
        self.assertEqual(pinned,(self.root/'updates/latest.json').read_bytes())
        future=publisher.publish(self.root,self.bundle('0.6.1-dev'),self.private)
        self.assertNotIn('minimum_version',future)
        self.assertEqual(old_feed.read_bytes(),pinned)
        self.assertEqual(publisher.validate_feed(self.root)['release'],'0.6.1-dev')
        self.assertEqual(publisher.validate_feed(self.root,legacy=True)['release'],'0.6.0-dev')

    def test_future_signed_manifest_with_obsolete_field_is_refused(self):
        publisher.publish(self.root,self.bundle('0.6.0-dev'),self.private)
        future=publisher.publish(self.root,self.bundle('0.6.1-dev'),self.private)
        future['minimum_version']='0.0.0'
        body=json.dumps(future,separators=(',',':')).encode()
        sig=subprocess.run(['openssl','dgst','-sha256','-sign',str(self.private)],input=body,check=True,capture_output=True).stdout
        raw=json.dumps(dict(payload=body.decode(),signature=sig.hex())).encode()
        (self.root/'updates/releases/0.6.1-dev/manifest.json').write_bytes(raw)
        (self.root/'updates/latest.json').write_bytes(raw)
        with self.assertRaisesRegex(ValueError,'must omit'):
            publisher.validate_feed(self.root)

    def test_future_release_requires_publishing_transition_first(self):
        with self.assertRaises(ValueError):publisher.publish(self.root,self.bundle('0.6.1-dev'),self.private)
        self.assertFalse((self.root/'updates/latest.json').exists())
        self.assertFalse((self.root/'updates/releases/0.6.1-dev').exists())

    def test_transition_imports_existing_old_feed_sequence(self):
        previous=publisher.publish(self.root,self.bundle(),self.private)
        (self.root/'updates/latest.json').unlink() # Simulate a pre-0.6 checkout.
        bridge=publisher.publish(self.root,self.bundle('0.6.0-dev'),self.private)
        self.assertEqual(bridge['sequence'],previous['sequence']+1)

    def test_retry_preserves_signature_and_export_is_deterministic(self):
        bundle=self.bundle();publisher.publish(self.root,bundle,self.private)
        before=(self.root/'updates/manifest.json').read_bytes()
        publisher.publish(self.root,bundle,self.private)
        self.assertEqual((self.root/'updates/manifest.json').read_bytes(),before)
        a=self.base/'a.zip';b=self.base/'b.zip';publisher.export(self.root,a)
        for p in (self.root/'updates/releases').rglob('*'):
            if p.is_file():os.utime(p,(1800000000,1800000000))
        publisher.export(self.root,b);self.assertEqual(a.read_bytes(),b.read_bytes())
        with zipfile.ZipFile(a) as z:self.assertIsNone(z.testzip());self.assertIn('firmware.bin',z.namelist())
        with self.assertRaises(ValueError):publisher.export(self.root,a)
    def test_versions_and_sequences_increase_and_older_release_cannot_move_channel(self):
        publisher.publish(self.root,self.bundle(),self.private)
        second=publisher.publish(self.root,self.bundle('0.5.4-dev'),self.private)
        self.assertEqual(second['sequence'],2)
        before=(self.root/'updates/manifest.json').read_bytes()
        with self.assertRaises(ValueError):publisher.publish(self.root,self.bundle('0.5.2-dev'),self.private)
        self.assertEqual((self.root/'updates/manifest.json').read_bytes(),before)
    def test_reusing_version_with_changed_firmware_payload_is_refused(self):
        publisher.publish(self.root,self.bundle(),self.private)
        before=(self.root/'updates/manifest.json').read_bytes()
        with self.assertRaises(ValueError):publisher.publish(self.root,self.bundle(data=b'changed'),self.private)
        self.assertEqual((self.root/'updates/manifest.json').read_bytes(),before)
    def test_wrong_key_or_publicly_readable_key_is_refused_before_publication(self):
        bundle=self.bundle();wrong=self.base/'wrong.pem';pos_updates.keygen(wrong,self.base/'wrong-public.pem')
        with self.assertRaises(ValueError):publisher.publish(self.root,bundle,wrong)
        self.private.chmod(0o644)
        with self.assertRaises(ValueError):publisher.publish(self.root,bundle,self.private)
        self.assertFalse((self.root/'updates/manifest.json').exists())
    def test_modified_signature_or_payload_cannot_become_sequence_history(self):
        publisher.publish(self.root,self.bundle(),self.private)
        (self.root/'updates/releases/0.5.3-dev/payload/System/README.txt').write_bytes(b'corrupt')
        with self.assertRaises(ValueError):publisher.publish(self.root,self.bundle('0.5.4-dev'),self.private)
        self.assertFalse((self.root/'updates/releases/0.5.4-dev').exists())
    def test_offline_signed_feed_must_match_exact_bundle(self):
        bundle=self.bundle();publisher.publish(self.root,bundle,self.private)
        self.assertEqual(publisher.verify_bundle(self.root,bundle)['release'],'0.5.3-dev')
        with self.assertRaises(ValueError):publisher.verify_bundle(self.root,self.bundle(data=b'changed'))
    def test_wrong_archive_name_and_corrupt_download_are_refused(self):
        bundle=self.bundle();renamed=self.base/'wrong.zip';bundle.rename(renamed)
        Path(str(bundle)+'.sha256').rename(Path(str(renamed)+'.sha256'))
        Path(str(renamed)+'.sha256').write_text(hashlib.sha256(renamed.read_bytes()).hexdigest()+'  '+renamed.name+'\n')
        with self.assertRaises(ValueError):publisher.publish(self.root,renamed,self.private)
    def test_symlinked_publish_paths_and_extra_export_files_are_refused(self):
        publisher.publish(self.root,self.bundle(),self.private)
        (self.root/'updates/releases/0.5.3-dev/unexpected.txt').write_text('do not publish')
        with self.assertRaises(ValueError):publisher.export(self.root,self.base/'unexpected.zip')
        (self.root/'updates/publisher.json').unlink();(self.root/'updates/publisher.json').symlink_to(self.base/'outside')
        with self.assertRaises(ValueError):publisher.configuration(self.root)
    def test_owner_secret_setup_validates_key_before_calling_github(self):
        original=subprocess.run
        def fake(cmd,**kw):
            if cmd[:3]==['gh','secret','set']:
                self.assertEqual(kw['stdin'].read(),self.private.read_bytes());return subprocess.CompletedProcess(cmd,0)
            return original(cmd,**kw)
        with patch.object(publisher.subprocess,'run',side_effect=fake):publisher.configure_signing(self.root,self.private)

    def test_corrupt_archive_cannot_change_channel_or_publish_version(self):
        archive=self.bundle();archive.write_bytes(archive.read_bytes()+b'corruption')
        with self.assertRaises(ValueError):publisher.publish(self.root,archive,self.private)
        self.assertFalse((self.root/'updates/manifest.json').exists())


class UpdateAssetTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.name='pos-update-0.5.3-dev.zip'
        (self.root/self.name).write_bytes(b'new update bytes')
    def attach(self,existing=None,changed=False):
        import pos_release_assets
        calls=[]
        def fake(cmd,**kwargs):
            calls.append(cmd)
            if cmd[1:3]==['release','view']:
                return subprocess.CompletedProcess(cmd,0,stdout=json.dumps({'assets':[{'name':self.name}] if existing else []}))
            if cmd[1:3]==['release','download']:
                target=Path(cmd[cmd.index('--dir')+1])/self.name
                target.write_bytes(b'old published bytes' if changed else (self.root/self.name).read_bytes())
            return subprocess.CompletedProcess(cmd,0)
        with patch.object(pos_release_assets.subprocess,'run',side_effect=fake):
            pos_release_assets.attach('v0.5.3-dev',self.root,[self.name])
        return calls
    def test_new_asset_is_uploaded_without_clobber(self):
        calls=self.attach();self.assertEqual(calls[-1][1:3],['release','upload']);self.assertNotIn('--clobber',calls[-1])
    def test_matching_published_asset_is_kept(self):
        calls=self.attach(existing=True);self.assertFalse(any(c[1:3]==['release','upload'] for c in calls))
    def test_different_published_asset_is_never_replaced(self):
        with self.assertRaises(ValueError):self.attach(existing=True,changed=True)
