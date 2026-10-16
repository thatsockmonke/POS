"""Host deployment/transport tests. USB, LAN target and HTTPS host are simulated."""
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
from test_installer import posctl

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import pos_deploy
import pos_updates
import pos_validation


def esp_image(project='pos_installer'):
    header=bytearray(24);header[0]=0xe9;header[1]=1;header[2]=2;header[3]=0x20;header[12]=9;header[23]=1
    desc=bytearray(256);struct.pack_into('<I',desc,0,0xabcd5432)
    desc[16:25]=b'0.5.0-dev';desc[48:48+len(project)]=project.encode()
    data=bytes(header)+struct.pack('<II',0x3c000020,256)+desc
    crc=0xef
    for byte in desc:crc^=byte
    data+=bytes(15)+bytes([crc])
    return data+hashlib.sha256(data).digest()


class DeployTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='pos-deploy-');self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.key=self.root/'session.key';self.key.write_text('a'*16);self.key.chmod(0o600)
        self.target=dict(format=1,model=posctl.MODEL,device_id='aa:bb:cc:dd:ee:ff',build_id='b'*64,version='0.5.0-dev',
                         healthy=True,ota_confirmed=True,update_pending=False,accepted_sequence=0,deployment=0)
    def evidence(self):
        file=self.root/'actual-observation.txt';file.write_text('TEST FIXTURE, not an actual device observation')
        record=dict(path=file.name,sha256=posctl.sha256(file),bytes=file.stat().st_size)
        self.report=self.root/'validation.json'
        self.report.write_text(json.dumps(dict(format=1,device_id=self.target['device_id'],validated_build_id=self.target['build_id'],
            checks={c:dict(result='pass',basis='operator observation',note='simulated evidence for host tests',evidence=[record]) for c in pos_deploy.REQUIRED})))
        return file
    def update(self):
        private=self.root/'private.pem';self.public=self.root/'trusted.pem';fingerprint=pos_updates.keygen(private,self.public)
        payload=self.root/'payload/System';payload.mkdir(parents=True);(payload/'readme.txt').write_text('new asset')
        old=self.root/'installed.pos';old.write_text(json.dumps(dict(format=1,model=posctl.MODEL,version='0.5.0-dev',
            firmware=dict(path='firmware.bin',size=512,sha256='e'*64),files=[dict(path='System/readme.txt',size=3,sha256='f'*64)])))
        self.update_dir=self.root/'update'
        pos_updates.make_update(SimpleNamespace(kind='assets',firmware=None,installed_manifest=old,payload=payload.parent,
            release='0.5.1-dev',minimum_version='0.5.0-dev',sequence=1,base_url='https://updates.example.test/1/',
            private_key=private,output=self.update_dir),posctl)
        self.target['trusted_key']=fingerprint
        return (self.update_dir/'manifest.json').read_bytes()
    def args(self):
        return SimpleNamespace(device='http://192.168.1.8:8080',key_file=self.key,update_dir=self.update_dir,
            manifest_url='https://updates.example.test/latest.json',public_key=self.public,validation_report=self.report,evidence_root=self.root,wait=3)
    def test_private_key_file_and_lan_endpoint_validation(self):
        self.assertEqual(pos_deploy.key_file(self.key),'a'*16)
        self.key.chmod(0o644)
        with self.assertRaises(ValueError):pos_deploy.key_file(self.key)
        for url in ('http://8.8.8.8:8080','http://user@192.168.1.8:8080','http://192.168.1.8:8080/?key=secret','https://192.168.1.8:8080'):
            with self.assertRaises(ValueError):pos_deploy.connection(url)
    def test_evidence_is_bound_to_target_build_and_rechecked(self):
        file=self.evidence();pos_deploy.readiness(self.report,self.root,self.target,posctl.sha256)
        with self.assertRaises(ValueError):pos_deploy.readiness(self.report,self.root,{**self.target,'build_id':'c'*64},posctl.sha256)
        file.write_text('changed')
        with self.assertRaises(ValueError):pos_deploy.readiness(self.report,self.root,self.target,posctl.sha256)
    def test_unrun_check_blocks_deployment(self):
        self.evidence();value=json.loads(self.report.read_text());value['checks']['ota_rollback']['result']='not_run';self.report.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError,'ota_rollback'):pos_deploy.readiness(self.report,self.root,self.target,posctl.sha256)
    def test_collector_preserves_explicit_identity_and_never_infers_physical_pass(self):
        obs=self.root/'observations.json';obs.write_text(json.dumps(dict(format=1,device_id=self.target['device_id'],validated_build_id=self.target['build_id'],observations=[])))
        report=pos_validation.hardware_report(self.root,obs,self.root/'collected.json')
        self.assertEqual(report['device_id'],self.target['device_id']);self.assertFalse(report['physical_validation_complete'])
        self.assertEqual(report['checks']['ota_rollback']['result'],'not_run')
    def test_signed_local_package_and_payload_tampering(self):
        self.update();raw,payload,key,build=pos_deploy.validate_update(self.update_dir,self.public,posctl)
        self.assertEqual(key,self.target['trusted_key']);self.assertEqual(payload['kind'],'assets');self.assertIsNone(build)
        (self.update_dir/'payload/System/readme.txt').write_text('tampered')
        with self.assertRaises(ValueError):pos_deploy.validate_update(self.update_dir,self.public,posctl)
    def test_signature_is_verified_with_separately_trusted_key(self):
        self.update();other=self.root/'other.pem';pos_updates.keygen(self.root/'other-private.pem',other)
        import subprocess
        with self.assertRaises(subprocess.CalledProcessError):pos_deploy.validate_update(self.update_dir,other,posctl)
    def test_deploy_waits_for_matching_confirmed_sequence_and_build(self):
        raw=self.update();self.evidence()
        response=Mock();response.status=200;response.read.return_value=raw
        opener=Mock();opener.open.return_value.__enter__=Mock(return_value=response);opener.open.return_value.__exit__=Mock(return_value=False)
        after={**self.target,'accepted_sequence':1,'deployment':3}
        with patch.object(pos_deploy,'status',side_effect=[self.target,after]),patch.object(pos_deploy,'build_opener',return_value=opener),patch.object(pos_deploy,'request') as request:
            result=pos_deploy.deploy(self.args(),posctl);self.assertEqual(result['accepted_sequence'],1)
            self.assertEqual(request.call_args.args[2:4],('POST','/deploy'))
    def test_hosted_manifest_change_is_rejected_before_mutating_device(self):
        self.update();self.evidence();response=Mock(status=200);response.read.return_value=b'changed'
        context=Mock();context.__enter__=Mock(return_value=response);context.__exit__=Mock(return_value=False);opener=Mock();opener.open.return_value=context
        with patch.object(pos_deploy,'status',return_value=self.target),patch.object(pos_deploy,'build_opener',return_value=opener),patch.object(pos_deploy,'request') as request:
            with self.assertRaises(ValueError):pos_deploy.deploy(self.args(),posctl)
            request.assert_not_called()
    def test_pull_verifies_before_publication_and_preserves_partial_on_bad_hash(self):
        data=b'large file'*9000;connection=Mock();response=Mock(status=200)
        response.getheader.return_value=hashlib.sha256(data).hexdigest();response.read.side_effect=[data[:65536],data[65536:],b''];connection.getresponse.return_value=response
        out=self.root/'audio.wav'
        with patch.object(pos_deploy,'connection',return_value=connection):pos_deploy.pull('device',self.key,out,posctl)
        self.assertEqual(out.read_bytes(),data)
        response.getheader.return_value='0'*64;response.read.side_effect=[b'wrong',b''];bad=self.root/'bad.wav'
        with patch.object(pos_deploy,'connection',return_value=connection):
            with self.assertRaises(ValueError):pos_deploy.pull('device',self.key,bad,posctl)
        self.assertFalse(bad.exists());self.assertEqual(Path(str(bad)+'.part').read_bytes(),b'wrong')
    def test_push_streams_hash_header_and_never_embeds_secret_in_url(self):
        source=self.root/'song.mp3';source.write_bytes(b'x'*150000);c=Mock();c.getresponse.return_value=Mock(status=200)
        with patch.object(pos_deploy,'connection',return_value=c):pos_deploy.push('device',self.key,source,source.name,posctl)
        self.assertEqual(c.putrequest.call_args.args,('POST','/upload?name=song.mp3'))
        self.assertIn(unittest.mock.call('X-POS-SHA256',posctl.sha256(source)),c.putheader.call_args_list)
        self.assertEqual(b''.join(call.args[0] for call in c.send.call_args_list),source.read_bytes())
    def test_usb_flash_validates_layout_backup_identity_and_readback(self):
        image=bytearray(b'\xff'*(0x10000));image[:len(esp_image())]=esp_image()
        records=[(1,2,0x9000,0x4000,'nvs'),(1,0,0xd000,0x2000,'otadata'),(1,1,0xf000,0x1000,'phy_init'),
                 (0,0,0x10000,0x100000,'factory'),(0,0x10,0x110000,0x170000,'ota_0'),(0,0x11,0x280000,0x170000,'ota_1')]
        for i,(kind,sub,offset,size,name) in enumerate(records):struct.pack_into('<HBBII16sI',image,0x8000+i*32,0x50aa,kind,sub,offset,size,name.encode(),0)
        at=0x8000+6*32;image[at:at+32]=b"\xeb\xeb"+b"\xff"*14+hashlib.md5(image[0x8000:at]).digest()
        image+=esp_image();file=self.root/'installer.bin';file.write_bytes(image)
        backup=self.root/'factory.bin';backup.write_bytes(b'factory fixture'+bytes(4*1024*1024-len(b'factory fixture')))
        metadata=dict(format=1,chip="esp32s3",readback_verified=True,plain_development_flash=True,device_id=self.target['device_id'],bytes=backup.stat().st_size,sha256=posctl.sha256(backup))
        Path(str(backup)+'.json').write_text(json.dumps(metadata));args=SimpleNamespace(confirm_flash=True,image=file,sha256=posctl.sha256(file),factory_backup=backup,port='mock-port')
        with patch.object(posctl,'hardware_probe',return_value=metadata),patch.object(posctl,'hardware_command') as command:
            pos_deploy.flash(args,posctl);self.assertEqual([call.args[1] for call in command.call_args_list],['write_flash','verify_flash'])
        metadata['device_id']='00:11:22:33:44:55'
        with patch.object(posctl,'hardware_probe',return_value=metadata),patch.object(posctl,'hardware_command') as command:
            with self.assertRaises(ValueError):pos_deploy.flash(args,posctl)
            command.assert_not_called()
