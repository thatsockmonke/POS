"""Constrained POS/cJSON heap; crypto/stdio internals are outside this budget."""
import hashlib,json,os,shutil,struct,subprocess,tempfile,unittest,wave
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import pos_updates
class ManifestHeapTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory(prefix='pos-heap-build-');cls.runner=Path(cls.temp.name)/'heap'
  sdk=Path(os.environ['IDF_PATH']);cjson=sdk/'components/json/cJSON'
  src=[ROOT/'tests/heap_runner.c',cjson/'cJSON.c']
  for c in ('pos_install','pos_core','pos_update'):src+=sorted((ROOT/'components'/c).glob('*.c'))
  cmd=['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all']
  if os.environ.get('POS_TEST_ASAN'):cmd+=['-fsanitize=address','-fno-omit-frame-pointer','-no-pie']
  for c in ('pos_install','pos_core','pos_update'):cmd+=['-I',str(ROOT/'components'/c/'include')]
  subprocess.run(cmd+['-I',str(cjson),*map(str,src),'-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free','-lcrypto','-lm','-o',str(cls.runner)],check=True)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(prefix='pos-heap-');self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
 def record(self,path,data):return {'path':path,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}
 def package(self,count=5,version='0.6.4-dev'):
  return {'format':1,'model':'m5stack-cardputer-adv','version':version,'firmware':self.record('firmware.bin',b'f'*512),'files':[self.record(f'System/file-{i}.txt',f'file {i}'.encode()) for i in range(count)]}
 def run_heap(self,*args,ok=True):
  r=subprocess.run([str(self.runner),*map(str,args)],capture_output=True,text=True)
  self.assertEqual(r.returncode,0 if ok else 1,r.stdout+r.stderr);return r.stdout
 def test_five_file_manifest_fits_12k_total_and_8k_single_allocation(self):
  f=self.root/'manifest.pos';f.write_text(json.dumps(self.package()))
  self.assertIn('records=5',self.run_heap('load',f,12288,8192))
 def test_all_64_records_and_32k_manifest_still_supported(self):
  f=self.root/'manifest.pos';raw=json.dumps(self.package(64));f.write_text(raw+' '*(32768-len(raw)))
  self.assertIn('records=64',self.run_heap('load',f,1048576,65536))
  f.write_text(f.read_text()+' ');self.run_heap('load',f,1048576,65536,ok=False)
 def test_every_parse_allocation_failure_frees_owned_text_and_records(self):
  f=self.root/'manifest.pos';f.write_text(json.dumps(self.package()))
  self.run_heap('failures',f)
 def audio_fixture(self,channels=1):
  (self.root/'Recordings').mkdir(exist_ok=True)
  samples=[-32768,-10000,0,12345,32767]*401
  if channels==2:samples=[s for x in samples for s in (x,-x if x!=-32768 else 32767)]
  with wave.open(str(self.root/'Recordings/test.wav'),'wb') as w:
   w.setnchannels(channels);w.setsampwidth(2);w.setframerate(16000)
   w.writeframes(struct.pack('<'+'h'*len(samples),*samples))
  stereo=[s for i in range(0,len(samples),channels) for s in (samples[i],samples[i+channels-1])]
  return struct.pack('<'+'h'*len(stereo),*stereo)
 def test_wav_playback_fits_3k_heap_and_2k_largest_block_mono_and_stereo(self):
  for channels in (1,2):
   with self.subTest(channels=channels):
    expected=self.audio_fixture(channels)
    out=self.run_heap('audio',self.root,'Recordings/test.wav',3072,2048)
    self.assertIn('denied=0',out)
    self.assertEqual((self.root/'decoded.raw').read_bytes(),expected)
 def test_mp3_playback_works_without_a_large_contiguous_decoder_allocation(self):
  shutil.copyfile(ROOT/'tests/fixtures/tone.mp3',self.root/'tone.mp3')
  out=self.run_heap('audio',self.root,'tone.mp3',40000,20000)
  self.assertIn('denied=0',out)
  self.assertEqual((self.root/'decoded.raw').read_bytes(),(ROOT/'tests/fixtures/tone-reference.raw').read_bytes())
 def test_every_audio_allocation_failure_releases_partial_decoder(self):
  self.audio_fixture()
  shutil.copyfile(ROOT/'tests/fixtures/tone.mp3',self.root/'tone.mp3')
  for path in ('Recordings/test.wav','tone.mp3'):
   with self.subTest(path=path):self.run_heap('audio-failures',self.root,path,40000,20000)
 def test_low_heap_audio_failure_is_clean(self):
  self.audio_fixture()
  self.run_heap('audio',self.root,'Recordings/test.wav',1024,1024,ok=False)
 def test_held_target_history_capture_and_install_fit_32k_pos_heap(self):
  private=self.root/'private.pem';public=self.root/'public.pem';pos_updates.keygen(private,public)
  card=self.root/'card';system=card/'POS/System';system.mkdir(parents=True)
  old=self.package();raw=json.dumps(old);(system/'installed.pos').write_text(raw)
  (card/'POS/.pos-transaction').write_text(hashlib.sha256(raw.encode()).hexdigest())
  for i in range(5):(system/f'file-{i}.txt').write_text(f'file {i}')
  firmware=self.root/'old.bin';firmware.write_bytes(b'f'*512)
  def signed(p,seq,name):
   meta={'format':1,'model':p['model'],'release':p['version'],'sequence':seq,'kind':'firmware','base_url':'https://example.test/feed/','package':p}
   body=json.dumps(meta,separators=(',',':'));sig=subprocess.run(['openssl','dgst','-sha256','-sign',str(private)],input=body.encode(),capture_output=True,check=True).stdout
   f=self.root/name;f.write_text(json.dumps({'payload':body,'signature':sig.hex()}));return f
  oldsig=signed(old,14,'old.json');new=self.package(version='0.6.6-dev');new['firmware']=self.record('firmware.bin',b'n'*512)
  new['files'][0]=self.record('System/file-0.txt',b'new file')
  newsig=signed(new,15,'new.json');feed=self.root/'feed';(feed/'payload/System').mkdir(parents=True)
  (feed/'firmware.bin').write_bytes(b'n'*512)
  for i in range(5):(feed/f'payload/System/file-{i}.txt').write_bytes(b'new file' if i==0 else f'file {i}'.encode())
  before=(system/'file-0.txt').read_bytes()
  out=self.run_heap('chain',newsig,public,card,oldsig,firmware,'unused',feed)
  self.assertIn('denied=0',out)
  self.assertEqual((system/'file-0.txt').read_bytes(),b'new file')
  self.assertTrue((card/'pos-update.pos').exists())
  self.assertEqual(next((card/'POS/Updates').glob('*/old-System/file-0.txt')).read_bytes(),before)
