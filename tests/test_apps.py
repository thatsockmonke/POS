import csv
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import struct
import subprocess
import tempfile
import unittest
import wave

ROOT=Path(__file__).resolve().parents[1]
class AppsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='pos-apps-native-');cls.runner=Path(cls.build.name)/'apps'
        sdk=Path(os.environ.get('IDF_PATH','/workspace/pos-cloud/esp-idf-v5.5.5'));cjson=sdk/'components/json/cJSON'
        sources=[ROOT/'tests/apps_runner.c',ROOT/'components/pos_install/pos_hash.c',cjson/'cJSON.c',*sorted((ROOT/'components/pos_core').glob('*.c'))]
        args=['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all']
        for component in ('pos_core','pos_install'):args+=['-I',str(ROOT/'components'/component/'include')]
        subprocess.run(args+['-I',str(cjson),*map(str,sources),'-lcrypto','-lm','-o',str(cls.runner)],check=True)
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='pos-apps-test-');self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        for path in ('Documents','Downloads','Music','Music/Playlists'): (self.root/path).mkdir(exist_ok=True)
    def run_app(self,mode,*args,expected=0,limit=None):
        def constrain():resource.setrlimit(resource.RLIMIT_AS,(limit,limit))
        result=subprocess.run([str(self.runner),mode,str(self.root),*map(str,args)],capture_output=True,preexec_fn=constrain if limit else None)
        self.assertEqual(result.returncode,expected,result.stdout+result.stderr);return result.stdout
    def write(self,name,data):
        p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);return p
    def test_directory_copy_preserves_files_empty_directories_and_originals(self):
        self.write('Documents/source/a.txt',b'hello'*40000);self.write('Documents/source/deep/b.txt',b'other');(self.root/'Documents/source/empty').mkdir()
        self.run_app('files','copy','Documents/source','Documents/copy')
        for name in ('a.txt','deep/b.txt'):self.assertEqual((self.root/'Documents/copy'/name).read_bytes(),(self.root/'Documents/source'/name).read_bytes())
        self.assertTrue((self.root/'Documents/copy/empty').is_dir())
    def test_copy_move_and_rename_never_replace_existing_destinations(self):
        a=self.write('Documents/a.txt',b'original');b=self.write('Documents/b.txt',b'keep')
        for op in ('copy','move'):self.run_app('files',op,'Documents/a.txt','Documents/b.txt',expected=1)
        self.assertEqual(a.read_bytes(),b'original');self.assertEqual(b.read_bytes(),b'keep')
        self.run_app('files','move','Documents/a.txt','Documents/renamed.txt');self.assertFalse(a.exists());self.assertEqual((a.parent/'renamed.txt').read_bytes(),b'original')
    def test_copy_cancellation_keeps_original_and_partial_unpublished(self):
        p=self.write('Documents/source.bin',b'abc'*30000);self.run_app('files','cancel-copy','Documents/source.bin','Documents/copy.bin',expected=1)
        self.assertFalse((self.root/'Documents/copy.bin').exists());self.assertEqual(p.read_bytes(),b'abc'*30000);self.assertTrue((self.root/'Documents/copy.bin.pos-copy.part').exists())
    def test_system_containers_traversal_and_symlinks_are_protected(self):
        for path in ('POS','POS/Users','POS/Users/Owner','POS/System/file','pos/system/file','POS/Users/SETTINGS.0.json','../outside','Documents/../a'):
            self.run_app('files','mutable',path,expected=1)
        outside=self.root/'outside';outside.mkdir();(self.root/'Documents/link').symlink_to(outside,target_is_directory=True)
        self.run_app('files','folder','Documents/link/child',expected=1);self.assertFalse((outside/'child').exists())
    def test_recursive_copy_refuses_nested_destination_and_symlink_child(self):
        self.write('Documents/source/file',b'keep');self.run_app('files','copy','Documents/source','Documents/source/nested',expected=1)
        (self.root/'Documents/source/link').symlink_to(self.root/'Documents/source/file')
        self.run_app('files','copy','Documents/source','Documents/copy',expected=1);self.assertFalse((self.root/'Documents/copy/link').exists())
    def test_trash_restore_round_trip_and_restore_collision_preserve_both(self):
        p=self.write('Documents/a.txt',b'original');entry=self.run_app('files','trash','Documents/a.txt').decode().strip();self.assertFalse(p.exists())
        self.assertEqual((self.root/entry/'data').read_bytes(),b'original');metadata=json.loads((self.root/entry/'restore.json').read_text());self.assertEqual(metadata['original'],'Documents/a.txt')
        p.write_bytes(b'new');self.run_app('files','restore',entry,expected=1);self.assertEqual(p.read_bytes(),b'new');self.assertEqual((self.root/entry/'data').read_bytes(),b'original')
        p.unlink();self.run_app('files','restore',entry);self.assertEqual(p.read_bytes(),b'original')
    def test_forged_trash_metadata_cannot_replace_system_data(self):
        self.write('Documents/a.txt',b'keep');entry=self.run_app('files','trash','Documents/a.txt').decode().strip();self.write(entry+'/restore.json',json.dumps({'format':1,'original':'POS/System/boot'}).encode())
        self.run_app('files','restore',entry,expected=1);self.assertTrue((self.root/entry/'data').exists())
    def test_explicit_permanent_delete_is_bounded_and_refuses_system(self):
        self.write('Documents/remove/deep/a',b'delete');self.run_app('files','remove','Documents/remove');self.assertFalse((self.root/'Documents/remove').exists())
        p=self.write('POS/System/file',b'keep');self.run_app('files','remove','POS/System/file',expected=1);self.assertEqual(p.read_bytes(),b'keep')
    def offer(self,data):
        self.write('Documents/source.bin',data);self.run_app('share','offer','Documents/source.bin');return self.root/'offer.bin'
    def test_share_offer_wire_format_and_minimum_mtu_fragments(self):
        data=b'file contents'*200;self.offer(data);wire=(self.root/'offer.bin').read_bytes();self.assertEqual(len(wire),158);self.assertEqual(wire[0],1);self.assertEqual(struct.unpack_from('<I',wire,9)[0],len(data));self.assertEqual(wire[94:].decode(),hashlib.sha256(data).hexdigest());self.run_app('share','fragments')
    def test_share_resume_duplicate_ack_and_verified_publication(self):
        data=bytes(range(256))*300;self.offer(data);self.run_app('share','write',12345);self.assertEqual(int(self.run_app('share','offset')),12345)
        self.run_app('share','replay');self.assertEqual(int(self.run_app('share','offset')),12345)
        self.run_app('share','wrong-offset',expected=1);self.assertFalse((self.root/'Downloads/source.bin').exists())
        self.run_app('share','write');self.run_app('share','finish');self.assertEqual((self.root/'Downloads/source.bin').read_bytes(),data)
        before=(self.root/'Downloads/source.bin').stat().st_mtime_ns
        self.assertEqual(int(self.run_app('share','offset')),len(data));self.assertEqual((self.root/'Downloads/source.bin').read_bytes(),data);self.assertEqual((self.root/'Downloads/source.bin').stat().st_mtime_ns,before)
    def test_share_bad_hash_and_truncation_preserve_partial_and_source(self):
        data=b'x'*2000;self.offer(data);self.run_app('share','write',500);self.run_app('share','finish',expected=1);self.run_app('share','write');part=next((self.root/'Downloads').glob('*.part'));part.write_bytes(b'z'*2000)
        self.run_app('share','finish',expected=1);self.assertFalse((self.root/'Downloads/source.bin').exists());self.assertEqual(part.read_bytes(),b'z'*2000);self.assertEqual((self.root/'Documents/source.bin').read_bytes(),data)
    def test_share_rejects_bad_names_and_metadata_conflicts(self):
        self.offer(b'file');wire=bytearray((self.root/'offer.bin').read_bytes());wire[13:94]=b'../escape\0'.ljust(81,b'\0');(self.root/'offer.bin').write_bytes(wire);self.run_app('share','offset',expected=1)
        wire[13:94]=b'source.bin\0'.ljust(81,b'\0');(self.root/'offer.bin').write_bytes(wire);self.run_app('share','offset');meta=next((self.root/'Downloads').glob('*.offer'));meta.write_bytes(b'damaged');self.run_app('share','offset',expected=1);self.assertEqual(meta.read_bytes(),b'damaged')
    def test_wav_mono_stereo_and_volume_match_independent_pcm(self):
        for channels in (1,2):
            with self.subTest(channels=channels):
                (self.root/'decoded.raw').unlink(missing_ok=True)
                samples=[-20000,-10000,0,10000,20000]*300
                if channels==2:samples=[x for v in samples for x in (v,-v)]
                p=self.root/'Music/tone.wav'
                with wave.open(str(p),'wb') as w:w.setnchannels(channels);w.setsampwidth(2);w.setframerate(22050);w.writeframes(struct.pack('<'+'h'*len(samples),*samples))
                result=self.run_app('music','Music/tone.wav',25);expected=[]
                for i in range(0,len(samples),channels):expected.extend([int(samples[i]/4),int(samples[i+channels-1]/4)])
                self.assertEqual((self.root/'decoded.raw').read_bytes(),struct.pack('<'+'h'*len(expected),*expected));self.assertTrue(result.startswith(b'22050 '));(self.root/'decoded.raw').unlink()
    def test_large_wav_streams_with_less_address_space_than_the_document(self):
        path=self.root/'Music/large.wav'
        size=64*1024*1024
        with path.open('wb') as out:
            out.write(b'RIFF'+struct.pack('<I',size+36)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,22050,44100,2,16)+b'data'+struct.pack('<I',size))
            block=struct.pack('<h',1000)*32768
            for _ in range(size//len(block)):out.write(block)
        result=self.run_app('music','Music/large.wav',100,limit=48*1024*1024)
        self.assertEqual(int(result.split()[2]),size//2)
        self.assertEqual((self.root/'decoded.raw').stat().st_size,size*2)
    def test_invalid_mp3_is_rejected_without_a_playback_loop(self):
        self.write('Music/bad.mp3',b'not an MP3'*10000);self.run_app('music','Music/bad.mp3',expected=1)
    def test_only_valid_gnss_sentences_can_mark_receiver_presence(self):
        self.assertEqual(self.gps(self.nmea('HELLO,1,2,3'))['sentences'],0)
        result=self.gps(self.nmea('GNRMC,120001,V,,,,,,,151026,,,N'))
        self.assertEqual(result['sentences'],1);self.assertFalse(result['fix'])
    def test_mp3_fixture_streams_to_nonzero_pcm(self):
        shutil.copyfile(ROOT/'tests/fixtures/tone.mp3',self.root/'Music/tone.mp3');result=self.run_app('music','Music/tone.mp3');rate,channels,frames,total=map(int,result.split());self.assertEqual(rate,22050);self.assertEqual(channels,1);self.assertGreater(frames,2000);self.assertEqual((self.root/'decoded.raw').read_bytes(),(ROOT/'tests/fixtures/tone-reference.raw').read_bytes())
    def test_wav_rejects_float_24bit_and_truncated_chunks(self):
        good=b'RIFF'+struct.pack('<I',40)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,16000,32000,2,16)+b'data'+struct.pack('<I',4)+b'\0'*4
        for index,data in enumerate((good[:-2],good[:20]+struct.pack('<H',3)+good[22:],good[:34]+struct.pack('<H',24)+good[36:])):
            self.write('Music/bad.wav',data);self.run_app('music','Music/bad.wav',expected=1)
    def test_music_library_and_plain_m3u_playlist(self):
        self.write('Music/a.mp3',b'placeholder');self.write('Music/b.WAV',b'placeholder');self.write('Music/ignore.txt',b'not audio')
        self.assertEqual(self.run_app('music','track','Music',0).splitlines()[0],b'2')
        self.run_app('music','add','Music/Playlists/favorites.m3u','Music/a.mp3');self.run_app('music','add','Music/Playlists/favorites.m3u','Music/b.WAV')
        self.assertEqual((self.root/'Music/Playlists/favorites.m3u').read_text(),'/Music/a.mp3\n/Music/b.WAV\n');self.assertEqual(self.run_app('music','playlist','Music/Playlists/favorites.m3u',1),b'2\nMusic/b.WAV\n')
    def nmea(self,body):return ('$'+body+'*'+format(__import__('functools').reduce(lambda a,b:a^b,body.encode(),0),'02X')+'\r\n').encode()
    def gps(self,data,chunk=1,now=1000):self.write('gps.nmea',data);return json.loads(self.run_app('gps',chunk,now))
    def test_gps_checksum_chunking_fix_coordinates_time_and_staleness(self):
        data=self.nmea('GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W')+self.nmea('GNGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,')
        for chunk in (1,7,128):
            fix=self.gps(data,chunk);self.assertTrue(fix['fix']);self.assertAlmostEqual(fix['latitude'],48.1173,6);self.assertAlmostEqual(fix['longitude'],11.516666666,6);self.assertEqual(fix['satellites'],8);self.assertEqual(fix['utc'],764426119);self.assertEqual(fix['sentences'],2)
        self.assertFalse(self.gps(data,now=7000)['fix'])
    def test_gps_invalid_checksum_overflow_coordinates_and_void_fix(self):
        good=self.nmea('GNRMC,120000,A,3400.000,S,11800.000,W,0,0,151026,,,A')
        fix=self.gps(b'noise'+b'$'+b'x'*500+b'\r\n'+good);self.assertTrue(fix['fix']);self.assertEqual(fix['latitude'],-34);self.assertEqual(fix['longitude'],-118)
        corrupt=bytearray(good);corrupt[10]^=1;self.assertFalse(self.gps(corrupt)['fix'])
        self.assertFalse(self.gps(good+self.nmea('GNRMC,120001,V,,,,,,,151026,,,N'))['fix'])
        self.assertFalse(self.gps(self.nmea('GNGGA,120000,9100.0,N,11800.0,W,1,8,1,10,M,0,M,,'))['fix'])
    def test_maps_requires_detected_gps_and_lora_apps_require_enabled_cap(self):self.run_app('capabilities')
    def test_radio_outbox_is_published_after_verification_and_never_overwrites(self):
        import zlib
        self.run_app('outbox','abcdef','original')
        path=self.root/'POS/Users/Owner/Radio/outbox/00abcdef.posr';wire=path.read_bytes()
        self.assertEqual(wire[:6],b'POSR\x01\x01');self.assertEqual(wire[19:-4],b'original');self.assertEqual(struct.unpack('<I',wire[-4:])[0],zlib.crc32(wire[:-4]))
        self.run_app('outbox','abcdef','replacement',expected=1);self.assertEqual(path.read_bytes(),wire)
        self.assertIn(b'replacement',path.with_suffix('.posr.part').read_bytes())
    def test_radio_invalid_packet_never_creates_completed_outbox_file(self):
        self.run_app('outbox','0','invalid id',expected=1)
        self.assertEqual(list((self.root/'POS/Users/Owner/Radio/outbox').iterdir()),[])
    def test_radio_crc_dedup_acks_retries_broadcast_and_range_csv(self):
        self.run_app('radio');rows=list(csv.DictReader((self.root/'range.csv').open()));self.assertEqual(len(rows),2);self.assertEqual(rows[0]['latitude'],'');self.assertEqual(rows[0]['rssi_dbm'],'');self.assertEqual(rows[1]['latitude'],'34.0000000');self.assertEqual(rows[1]['rssi_dbm'],'-91');self.assertEqual(rows[1]['snr_db'],'3.50')

    def test_recorder_wav_is_standard_streamed_pcm_and_unique(self):
        self.run_app('recorder','finish'); self.run_app('recorder','finish')
        files=sorted((self.root/'Recordings').glob('*.wav')); self.assertEqual(len(files),2)
        with wave.open(str(files[0]),'rb') as wav:
            self.assertEqual((wav.getnchannels(),wav.getsampwidth(),wav.getframerate(),wav.getnframes()),(1,2,16000,38400))
            expected=struct.pack('<256h',*[i*251-32000 for i in range(256)])
            self.assertEqual(wav.readframes(38400),expected*150)
        self.assertFalse(list((self.root/'Recordings').glob('*.part')))
    def test_recorder_partial_recovery_and_corrupt_readback(self):
        self.run_app('recorder','partial'); folder=self.root/'Recordings'
        part=folder/'Recording-00001.wav.part'; self.assertTrue(part.exists())
        self.run_app('recorder','recover','Recordings/'+part.name)
        self.assertFalse(part.exists())
        with wave.open(str(folder/'Recording-00001.wav'),'rb') as wav:self.assertEqual(wav.getnframes(),38400)
        self.run_app('recorder','corrupt',expected=1)
        self.assertFalse((folder/'Recording-00002.wav').exists()); self.assertTrue((folder/'Recording-00002.wav.part').exists())
    def test_recorder_rejects_symlink_odd_partial_and_existing_wav(self):
        outside=self.root/'outside'; outside.mkdir(); (self.root/'Recordings').symlink_to(outside,target_is_directory=True)
        self.run_app('recorder','finish',expected=1); self.assertEqual(list(outside.iterdir()),[])
        (self.root/'Recordings').unlink(); self.run_app('recorder','partial')
        part=self.root/'Recordings/Recording-00001.wav.part'; part.write_bytes(part.read_bytes()+b'x')
        before=part.read_bytes();self.run_app('recorder','recover','Recordings/'+part.name,expected=1);self.assertEqual(part.read_bytes(),before)
    def test_imu_full_configuration_scaling_and_nec_wire_bits(self):self.run_app('instruments')
    def app_envelope(self,schema,generation,payload,format=1):
        import zlib
        crc=zlib.crc32(f'counter\n{schema}\n{generation}\n'.encode()+payload.encode())
        return json.dumps(dict(format=format,app='counter',schema=schema,generation=generation,payload=payload,crc32=f'{crc:08x}')).encode()
    def test_counter_legacy_migration_preserves_original_and_generational_save(self):
        original=self.write('POS/Users/Owner/Saves/counter.txt',b'321\n')
        self.assertEqual(self.run_app('counter').decode().strip(),'321');self.assertEqual(original.read_bytes(),b'321\n')
        self.assertEqual(self.run_app('counter','999').decode().strip(),'999');self.assertEqual(self.run_app('counter').decode().strip(),'999')
        folder=self.root/'POS/Users/Owner/Saves/counter';self.assertEqual(len(list(folder.glob('slot-*.json'))),2)
    def test_counter_schema_migration_keeps_exact_old_envelope(self):
        legacy=self.app_envelope(0,1,'{"value":-456}')
        self.write('POS/Users/Owner/Saves/counter/slot-1.json',legacy)
        self.assertEqual(self.run_app('counter').decode().strip(),'-456')
        backup=self.root/'POS/Users/Owner/Saves/counter/migration-0-1.json';self.assertEqual(backup.read_bytes(),legacy)
        self.run_app('counter','22');self.assertEqual(backup.read_bytes(),legacy)
    def test_future_save_and_corrupt_crc_are_preserved_without_default_writes(self):
        future=self.write('POS/Users/Owner/Saves/counter/slot-1.json',self.app_envelope(7,1,'{"unknown":7}'))
        before=future.read_bytes();self.run_app('counter','99',expected=1);self.assertEqual(future.read_bytes(),before)
        future.write_bytes(self.app_envelope(1,1,'{"count":1}',format=2));before=future.read_bytes()
        self.run_app('counter','99',expected=1);self.assertEqual(future.read_bytes(),before)
        corrupt=json.loads(self.app_envelope(1,1,'{"count":1}'));corrupt['crc32']='00000000';future.write_text(json.dumps(corrupt));before=future.read_bytes()
        self.run_app('counter','99',expected=1);self.assertEqual(future.read_bytes(),before)

    def test_app_save_interrupted_partial_blocks_replacement_but_old_slot_loads(self):
        self.run_app('counter','15')
        part=self.write('POS/Users/Owner/Saves/counter/slot-0.json.part',b'incomplete save')
        self.run_app('counter','99',expected=1)
        self.assertEqual(self.run_app('counter').decode().strip(),'15');self.assertEqual(part.read_bytes(),b'incomplete save')
    def test_counter_corrupt_legacy_preserves_original_and_refuses_reset(self):
        old=self.write('POS/Users/Owner/Saves/counter.txt',b'123\ntrailing corruption')
        before=old.read_bytes();self.run_app('counter',expected=1);self.assertEqual(old.read_bytes(),before)
        self.assertFalse((old.parent/'counter').exists())
