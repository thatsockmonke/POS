from pathlib import Path
import os
import struct
import subprocess
import tempfile
import unittest
import urllib.parse
import zlib
ROOT=Path(__file__).resolve().parents[1]

def osm(points=None):
    points=points or [(34.050,-118.248),(34.051,-118.247),(34.050,-118.247)]
    return ('<?xml version="1.0"?><osm version="0.6"><way id="1">'+''.join(f'<nd ref="{i}" lat="{lat}" lon="{lon}"/>' for i,(lat,lon) in enumerate(points))+'<tag k="highway" v="residential"/><tag k="name" v="Test &amp; Road"/></way></osm>').encode()

class MapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='pos-maps-build-')
        cls.runner=Path(cls.build.name)/'maps'
        sources=['tests/maps_runner.c']+['components/pos_core/'+p for p in ('pos_maps.c','pos_fs.c','pos_core.c','pos_render.c','pos_background.c','pos_visual.c')]
        subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-g','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(ROOT/'components/pos_core/include'),'-I',str(ROOT/'components/pos_install/include'),*map(lambda p:str(ROOT/p),sources),'-lm','-o',str(cls.runner)],check=True)
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='pos-map-');self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.raw=self.root/'region.osm';self.map=self.root/'region.pmap'
    def run_map(self,*args,ok=True):
        p=subprocess.run([str(self.runner),*map(str,args)],capture_output=True,text=True)
        self.assertEqual(p.returncode,0 if ok else 1,p.stdout+p.stderr);return p.stdout
    def compile(self,data=None):
        self.raw.write_bytes(osm() if data is None else data);return self.run_map('compile',self.raw,self.map)
    def world(self,path,lat=0,lon=0,zoom=0,ok=True):
        p=subprocess.run([str(self.runner),'world',str(path),str(lat),str(lon),str(zoom)],capture_output=True)
        self.assertEqual(p.returncode,0 if ok else 1,p.stderr)
        return p.stdout
    def test_world_asset_streams_at_poles_date_line_and_all_zooms(self):
        asset=ROOT/'sd-payload/System/Assets/world.map'
        images=set()
        for lat,lon in ((0,0),(34.05,-118.248),(90,180),(-90,-180),(0,179.9),(0,-179.9)):
            for zoom in range(9):
                pixels=self.world(asset,lat,lon,zoom)
                self.assertEqual(len(pixels),2640)
                images.add(pixels)
        self.assertGreater(len(images),20)
        self.run_map('world-geometry')
    def test_world_asset_corruption_bounds_and_trailing_bytes_are_rejected(self):
        data=(ROOT/'sd-payload/System/Assets/world.map').read_bytes()
        for bad in (data[:-1],data+b'x',b'BAD!'+data[4:],data[:30]+bytes([data[30]^1])+data[31:]):
            self.raw.write_bytes(bad);self.world(self.raw,ok=False)
        asset=ROOT/'sd-payload/System/Assets/world.map'
        for lat,lon,zoom in ((91,0,0),(0,181,0),('nan',0,0),(0,0,9)):
            self.world(asset,lat,lon,zoom,ok=False)
    def test_world_date_line_segment_never_draws_a_line_across_the_planet(self):
        segment=struct.pack('<hhhh',17900,0,-17900,0)
        self.raw.write_bytes(b'PWM1'+struct.pack('<II',1,zlib.crc32(segment))+segment)
        pixels=self.world(self.raw,0,0,0)
        self.assertLess(sum(b.bit_count() for b in pixels),5)
        pixels=self.world(self.raw,0,180,0)
        self.assertGreater(sum(b.bit_count() for b in pixels),0)

    def test_compile_preserves_portable_source_and_checksums_header_and_geometry(self):
        self.compile();self.assertEqual(self.raw.read_bytes(),osm());data=self.map.read_bytes()
        self.assertEqual(data[:4],b'PMP1');self.assertEqual(len(data),64+2*16)
        expected=struct.unpack_from('<I',data,20)[0];self.assertEqual(zlib.crc32(data[:20]+b'\0'*4+data[24:]),expected)
        self.assertGreater(int(self.run_map('raster',self.map)),0)
    def test_does_not_connect_separate_ways(self):
        data=b'<osm version="0.6">'+b''.join(b'<way id="1"><nd lat="34.05" lon="-118.248"/><nd lat="34.051" lon="-118.247"/></way>' for _ in range(2))+b'</osm>'
        self.compile(data);self.assertEqual(len(self.map.read_bytes()),64+2*16)
    def test_corrupt_header_geometry_truncation_and_trailing_data_refused(self):
        self.compile();original=self.map.read_bytes()
        for data in (original[:-1],original+b'x',original[:25]+b'X'+original[26:],original[:70]+bytes([original[70]^1])+original[71:]):
            self.map.write_bytes(data);self.run_map('raster',self.map,ok=False)
    def test_malformed_or_unusable_responses_never_publish_a_map(self):
        for data in (b'',b'<html>Error</html>',osm()[:-6],osm()+b'oops',b'<osm version="0.6"><remark>timeout</remark></osm>',osm().replace(b'34.05',b'nan'),osm().replace(b'-118.',b'-999.'),b'<osm version="0.6"></osm>',osm().replace(b'lat=',b'notlat=')):
            with self.subTest(data=data[:70]):
                self.raw.write_bytes(data);self.run_map('compile',self.raw,self.map,ok=False);self.assertFalse(self.map.exists())
                self.map.with_suffix('.pmap.part').unlink(missing_ok=True)
    def test_cancellation_and_existing_files_are_preserved(self):
        self.raw.write_bytes(osm());self.run_map('compile',self.raw,self.map,'cancel',ok=False);self.assertFalse(self.map.exists())
        part=self.map.with_suffix('.pmap.part');before=part.read_bytes()
        self.run_map('compile',self.raw,self.map,ok=False);self.assertEqual(part.read_bytes(),before)
        part.unlink();self.compile();before=self.map.read_bytes();self.run_map('compile',self.raw,self.map,ok=False);self.assertEqual(self.map.read_bytes(),before)
        self.run_map('raster',self.map,'cancel',ok=False)
    def test_oversized_download_and_excessive_geometry_are_refused(self):
        with self.raw.open('wb') as f:f.truncate(64*1024*1024+1);self.run_map('compile',self.raw,self.map,ok=False)
        self.raw.write_bytes(b'<osm version="0.6"><way id="1">'+b'<nd lat="34.05" lon="-118.24"/><nd lat="34.051" lon="-118.241"/>'*15001+b'</way></osm>')
        self.run_map('compile',self.raw,self.map,ok=False);self.assertFalse(self.map.exists())
    def test_long_attributes_quotes_and_nd_boundaries(self):
        self.compile(osm().replace(b'<tag k="name" v="Test &amp; Road"/>',b'<tag k="name" v="'+b'a'*500+b'"/>').replace(b'lat="34.05"',b"lat = '34.05'"))
        self.assertGreater(int(self.run_map('raster',self.map)),0)
    def test_quoted_angle_bracket_in_a_name_does_not_break_geometry(self):
        self.compile(osm().replace(b'Test &amp; Road', b'Road > Alley'))
        self.assertEqual(len(self.map.read_bytes()),64+2*16)
    def test_query_is_bounded_encoded_and_contains_no_personal_identifiers(self):
        url=self.run_map('query',34,0,1000,1).strip();self.assertLess(len(url),512)
        q=urllib.parse.parse_qs(urllib.parse.urlparse(url).query)['data'][0]
        self.assertIn('[maxsize:67108864]',q);self.assertIn('way["highway"]',q);self.assertIn('rel(bw.r)[type=restriction]',q);self.assertTrue(q.endswith('out tags;'))
        for provider in (1,2):self.run_map("query",34,0,1000,provider)
        direct=self.run_map("query",34,0,500,0).strip()
        parsed=urllib.parse.urlparse(direct)
        self.assertEqual(parsed.netloc,'api.openstreetmap.org')
        bbox=list(map(float,urllib.parse.parse_qs(parsed.query)['bbox'][0].split(',')))
        self.assertLess(bbox[0],0);self.assertGreater(bbox[2],0)
        self.assertLess(bbox[1],34);self.assertGreater(bbox[3],34)
        for latitude in (0,34,80):
            for width in (500,1000,2000,4000):
                url=self.run_map("query",latitude,0,width,0)
                box=list(map(float,urllib.parse.parse_qs(urllib.parse.urlparse(url).query)['bbox'][0].split(',')))
                self.assertLess((box[2]-box[0])*(box[3]-box[1]),0.25)
        self.run_map("query",34,0,1000,3,ok=False)
        for args in [('nan',0,1000),(81,0,1000),(0,180,1000),(0,0,5000),(0,0,0)]:self.run_map('query',*args,ok=False)
    def test_north_up_projection_bearing_and_marker_rendering(self):self.run_map('geometry')
    def test_extremely_long_segments_clip_in_bounded_time(self):
        self.compile(osm([(-80,-179),(80,179)]));self.run_map('raster',self.map)

    def test_4km_map_and_xml_larger_than_old_8mib_limit_stream_successfully(self):
        with self.raw.open('wb') as output:
            for _ in range(36):output.write(b' '*(256*1024))
            output.write(osm())
        self.run_map('compile-4km',self.raw,self.map)
        self.assertEqual(struct.unpack_from('<I',self.map.read_bytes(),12)[0],4000)
        self.assertGreater(int(self.run_map('raster',self.map)),0)
