from pathlib import Path
import hashlib,heapq,struct,subprocess,tempfile,unittest,zlib
ROOT=Path(__file__).resolve().parents[1]
NODES={1:(34.05,-118.248),2:(34.05,-118.247),3:(34.051,-118.247),4:(34.051,-118.248),5:(34.05,-118.246),6:(34.051,-118.246)}
def way(i,nodes,name='Main Road',tags=None):
    alltags={'highway':'residential','name':name};alltags.update(tags or {})
    return f'<way id="{i}">'+''.join(f'<nd ref="{n}" lat="{NODES[n][0]}" lon="{NODES[n][1]}"/>' for n in nodes)+''.join(f'<tag k="{k}" v="{v}"/>' for k,v in alltags.items())+'</way>'
def relation(fr,to,via,rule='no_left_turn',extra=''):
    return f'<relation id="999"><member type="way" ref="{fr}" role="from"/><member type="way" ref="{to}" role="to"/><member type="node" ref="{via}" role="via"/><tag k="type" v="restriction"/><tag k="restriction" v="{rule}"/>{extra}</relation>'
def xml(*parts):return ('<osm version="0.6">'+''.join(parts)+'</osm>').encode()
class NavigationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory();cls.runner=Path(cls.build.name)/'navigation'
        sources=['tests/navigation_runner.c']+['components/pos_core/'+n for n in ['pos_navigation.c','pos_maps.c','pos_location.c','pos_fs.c']]
        subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-g','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(ROOT/'components/pos_core/include'),'-I',str(ROOT/'components/pos_install/include'),*map(lambda s:str(ROOT/s),sources),'-lm','-o',str(cls.runner)],check=True)
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);self.raw=self.root/'region.osm';self.map=self.root/'region.pmap';self.routefile=self.root/'trip.route'
    def run_nav(self,*args,ok=True):
        p=subprocess.run([str(self.runner),*map(str,args)],capture_output=True,text=True,timeout=60)
        self.assertNotIn("runtime error:",p.stderr)
        self.assertEqual(p.returncode,0 if ok else 1,p.stdout+p.stderr);return p.stdout
    def build_map(self,*parts):self.raw.write_bytes(xml(*parts));self.run_nav('build',self.raw,self.map)
    def test_direct_osm_import_resolves_unsorted_64_bit_nodes_and_filters_nonroads(self):
        ids={i:2**40+i for i in (1,2,3)}
        direct=''.join(f'<node id="{ids[i]}" lat="{NODES[i][0]}" lon="{NODES[i][1]}"/>' for i in (3,1,2))
        direct+=f'<way id="10"><nd ref="{ids[1]}"/><nd ref="{ids[2]}"/><tag k="highway" v="residential"/><tag k="name" v="A &gt; B"/></way>'
        direct+=f'<way id="20"><nd ref="{ids[2]}"/><nd ref="{ids[3]}"/><tag k="highway" v="residential"/></way>'
        direct+=f'<way id="30"><nd ref="{ids[1]}"/><nd ref="{ids[3]}"/><tag k="building" v="yes"/></way>'
        direct+='<relation id="500"><member type="way" ref="30" role="outer"/><tag k="type" v="multipolygon"/></relation>'
        direct+=relation(10,20,ids[2])
        self.raw.write_bytes(xml(direct));original=self.raw.read_bytes();prepared=self.root/'roads.osm'
        self.run_nav('import',self.raw,prepared)
        self.assertEqual(self.raw.read_bytes(),original)
        data=prepared.read_bytes();self.assertNotIn(b'building',data);self.assertNotIn(b'multipolygon',data)
        self.assertIn(b'restriction',data);self.assertIn(b'lat="34.050000"',data)
        self.run_nav('build',prepared,self.map)
        self.route(1,3,4,ok=False);self.route(1,2)
        self.assertEqual(len(self.map.read_bytes()),64+2*16)

    def test_osm_import_rejects_missing_duplicate_invalid_and_truncated_nodes(self):
        good='<node id="1" lat="34.05" lon="-118.248"/><node id="2" lat="34.05" lon="-118.247"/>'
        road='<way id="10"><nd ref="1"/><nd ref="2"/><tag k="highway" v="residential"/></way>'
        prepared=self.root/'roads.osm'
        for source in (xml(road), xml(good,good,road), xml(good.replace('34.05','nan'),road),xml(good,road)[:-6],xml(good,road)+b'junk',b'<html>error</html>'):
            self.raw.write_bytes(source);self.run_nav('import',self.raw,prepared,ok=False)
            self.assertFalse(prepared.exists());self.assertFalse(list(self.root.glob('*.work-*')))

    def test_osm_import_cancel_collision_and_long_description_preserve_originals(self):
        direct=xml('<node id="1" lat="34.05" lon="-118.248"/>',
                   '<node id="2" lat="34.05" lon="-118.247"/>',
                   '<way id="10"><nd ref="1"/><nd ref="2"/><tag k="description" v="'+('x'*10000)+'"/><tag k="highway" v="residential"/></way>')
        self.raw.write_bytes(direct);prepared=self.root/'roads.osm'
        self.run_nav('import',self.raw,prepared,'cancel',ok=False)
        self.assertFalse(prepared.exists());self.assertEqual(self.raw.read_bytes(),direct)
        self.run_nav('import',self.raw,prepared);before=prepared.read_bytes()
        self.run_nav('import',self.raw,prepared,ok=False);self.assertEqual(prepared.read_bytes(),before)
        self.run_nav('build',prepared,self.map);self.route(1,2)

    def test_osm_import_external_index_spans_many_sort_batches_and_node_barriers_survive(self):
        parts=[f'<node id="{i}" lat="34.05" lon="{-118.248+i*.000001:.6f}"/>' for i in range(900,0,-1)]
        parts[900-450]='<node id="450" lat="34.05" lon="-118.247550"><tag k="barrier" v="bollard"/></node>'
        parts.append('<way id="10">'+''.join(f'<nd ref="{i}"/>' for i in range(1,901))+'<tag k="highway" v="residential"/></way>')
        self.raw.write_bytes(xml(*parts));prepared=self.root/'roads.osm'
        self.run_nav('import',self.raw,prepared);self.run_nav('build',prepared,self.map)
        self.assertIn(b'bollard',prepared.read_bytes());self.assertFalse(list(self.root.glob('*.work-*')))
    def route(self,start,goal,mode=4,ok=True):
        self.run_nav('route',self.map,self.routefile,*NODES[start],*NODES[goal],mode,ok=ok)
        if not ok:return []
        data=self.routefile.read_bytes();self.assertEqual(data[:4],b'PRT1');n=struct.unpack_from('<I',data,8)[0]
        self.assertEqual(len(data),64+80*n);check=struct.unpack_from('<I',data,20)[0];self.assertEqual(zlib.crc32(data[:20]+b'\0'*4+data[24:]),check)
        return [tuple(v/1e6 for v in struct.unpack_from('<ii',data,64+i*80)) for i in range(n)]
    def test_offline_shortest_route_and_turn_by_turn_names(self):
        self.build_map(way(10,[1,2],'First Street'),way(20,[2,3],'North Road'),way(30,[1,4,6,3],'Long Detour'))
        points=self.route(1,3);self.assertEqual(points,[NODES[n] for n in (1,2,3)])
        g=self.run_nav('guide',self.routefile,self.map,*NODES[1]).split('\t');values=list(map(int,g[0].split()));self.assertEqual(values[1],1);self.assertEqual(g[1].strip(),'North Road');self.assertGreater(values[2],80)
        values=list(map(int,self.run_nav('guide',self.routefile,self.map,*NODES[3]).split('\t')[0].split()));self.assertEqual(values[5],1)
        self.assertGreater(int(self.run_nav('render',self.routefile,self.map)),0)
    def test_one_way_and_bicycle_exception_and_walking_bidirectional(self):
        self.build_map(way(10,[1,2],tags={'oneway':'yes','oneway:bicycle':'no'}))
        self.route(2,1,4,ok=False);self.route(2,1,2);self.routefile.unlink();self.route(2,1,1)
    def test_mode_specific_oneway_and_directional_access(self):
        self.build_map(way(10,[1,2],tags={'oneway:motorcar':'yes','foot:backward':'no'}))
        self.route(2,1,4,ok=False);self.route(2,1,1,ok=False);self.route(2,1,2)
    def test_reverse_oneway_and_roundabout(self):
        self.build_map(way(10,[1,2],tags={'oneway':'-1'}));self.route(1,2,4,ok=False);self.route(2,1)
    def test_prohibited_turn_detours_and_except_profile(self):
        self.build_map(relation(10,20,2,extra='<tag k="except" v="bicycle"/>'),way(10,[1,2]),way(20,[2,3]),way(30,[2,5,6,3],'Detour'))
        points=self.route(1,3);self.assertIn(NODES[5],points);self.routefile.unlink();self.assertEqual(self.route(1,3,2),[NODES[n] for n in (1,2,3)])
    def test_only_turn_restriction_blocks_other_exits(self):
        self.build_map(relation(10,30,2,'only_right_turn'),way(10,[1,2]),way(20,[2,3]),way(30,[2,5,6,3]))
        self.assertIn(NODES[5],self.route(1,3))
    def test_crossings_without_shared_node_ids_never_connect(self):
        old=NODES.copy();NODES[7]=NODES[2]
        try:
            self.build_map(way(10,[1,2]),way(20,[7,3]));self.route(1,3,1,ok=False)
        finally:NODES.clear();NODES.update(old)
    def test_access_tag_order_mode_overrides_conditional_and_private(self):
        self.build_map(way(10,[1,2],tags={'foot':'yes','access':'private'}));self.route(1,2,4,ok=False);self.route(1,2,1)
    def test_long_nonrouting_descriptions_do_not_disable_navigation(self):
        self.build_map('<node id="2"><tag k="description" v="'+('a'*600)+'"/></node>',way(10,[1,2,3]))
        self.route(1,3)
    def test_generic_access_yes_does_not_make_footways_driveable(self):
        self.build_map(way(10,[1,2],tags={'highway':'footway','access':'yes'}))
        self.route(1,2,4,ok=False);self.route(1,2,1)
    def test_conditional_access_is_conservatively_blocked(self):
        self.build_map(way(10,[1,2],tags={'motor_vehicle:conditional':'yes @ (Mo-Fr)'}));self.route(1,2,4,ok=False);self.route(1,2,1)
    def test_barrier_blocks_vehicle_but_preserves_walk_and_cycle(self):
        self.build_map('<node id="2"><tag k="barrier" v="bollard"/></node>',way(10,[1,2,3]));self.route(1,3,4,ok=False);self.route(1,3,1)
    def test_road_search_decodes_entities_and_filters_access(self):
        self.build_map(way(10,[1,2],'Oak &amp; Pine'),way(20,[2,3],'Oak &amp; Pine'),way(30,[1,4],'Secret Lane',{'access':'private'}))
        results=self.run_nav('search',self.map,'oak',*NODES[1],1).splitlines();self.assertEqual(len(results),1);self.assertTrue(results[0].startswith('Oak & Pine\t'))
        self.assertEqual(self.run_nav('search',self.map,'Secret',*NODES[1],4),'')
    def test_route_corruption_and_wrong_graph_are_refused(self):
        self.build_map(way(10,[1,2,3]));self.route(1,3);original=self.routefile.read_bytes();self.routefile.write_bytes(original[:-1]);self.run_nav('guide',self.routefile,self.map,*NODES[1],ok=False)
        self.routefile.write_bytes(original);p=self.map.with_suffix('.pmap.pnav');b=p.read_bytes();p.write_bytes(b[:-1]);self.run_nav('guide',self.routefile,self.map,*NODES[1],ok=False)
    def test_cancellation_never_replaces_previous_route_and_workspace_is_removed(self):
        self.build_map(way(10,[1,2,3]));self.route(1,3);good=self.routefile.read_bytes()
        self.run_nav('route',self.map,self.root/'cancel.route',*NODES[1],*NODES[3],4,'cancel',ok=False)
        self.assertEqual(self.routefile.read_bytes(),good);self.assertFalse((self.root/'cancel.route').exists());self.assertFalse(list(self.root.glob('*.work-*')))
    def test_preexisting_workspaces_are_never_cleaned_up(self):
        self.build_map(way(10,[1,2,3]))
        destination=self.root/'blocked.route'
        for i in range(100):
            p=self.root/f'blocked.route.work-{i}';p.mkdir();(p/'precious').write_text('keep')
        self.run_nav('route',self.map,destination,*NODES[1],*NODES[3],4,ok=False)
        self.assertEqual(len(list(self.root.glob('blocked.route.work-*/precious'))),100)
    def test_external_sort_and_paged_a_star_exceed_ram_page_sizes(self):
        old=NODES.copy()
        try:
            for n in range(1,901):NODES[n]=(round(34.05+(n//30)*.00003,6),round(-118.248+(n%30)*.00003,6))
            parts=[]
            for row in range(30):parts.append(way(1000+row,list(range(row*30+1,row*30+31)),f'Row {row}'))
            for col in range(30):parts.append(way(2000+col,[row*30+col+1 for row in range(30)],f'Col {col}'))
            self.build_map(*parts);points=self.route(1,900);self.assertEqual(points[0],NODES[1]);self.assertEqual(points[-1],NODES[900]);self.assertGreater(len(points),30)
            data=self.map.with_suffix('.pmap.pnav').read_bytes();nn,ne=struct.unpack_from('<II',data,8)
            adjacent={i:[] for i in range(nn)}
            for i in range(ne):
                a,b,w,cost,mode=struct.unpack_from('<IIIII',data,64+nn*24+i*20)
                if mode&4:adjacent[a].append((b,cost))
            distances={0:0};queue=[(0,0)]
            while queue:
                cost,node=heapq.heappop(queue)
                if cost!=distances[node]:continue
                for nxt,length in adjacent[node]:
                    if cost+length<distances.get(nxt,10**30):distances[nxt]=cost+length;heapq.heappush(queue,(cost+length,nxt))
            self.assertEqual(struct.unpack_from('<I',self.routefile.read_bytes(),12)[0],distances[nn-1])
        finally:NODES.clear();NODES.update(old)
    def test_same_way_no_u_turn_does_not_forbid_continuing_forward(self):
        self.build_map(relation(10,10,2,'no_u_turn'),way(10,[1,2,3]))
        self.assertEqual(self.route(1,3),[NODES[n] for n in (1,2,3)])
    def test_via_way_restriction_blocks_affected_vehicle_way_conservatively(self):
        r=relation(10,20,30).replace('type="node" ref="30"','type="way" ref="30"')
        self.build_map(r,way(10,[1,2]),way(20,[2,3]))
        self.route(1,3,4,ok=False);self.route(1,3,1)
    def test_explicit_foot_turn_restriction_is_applied(self):
        r=relation(10,20,2).replace('k="restriction"','k="restriction:foot"')
        self.build_map(r,way(10,[1,2]),way(20,[2,3]),way(30,[2,5,6,3]))
        self.assertIn(NODES[5],self.route(1,3,1));self.routefile.unlink()
        self.assertEqual(self.route(1,3,4),[NODES[n] for n in (1,2,3)])
    def test_specific_motorcar_restriction_wins_independent_of_tag_order(self):
        r=relation(10,20,2).replace('<tag k="restriction" v="no_left_turn"/>','<tag k="restriction:motorcar" v="no_left_turn"/><tag k="restriction:vehicle" v="only_left_turn"/>')
        self.build_map(r,way(10,[1,2]),way(20,[2,3]),way(30,[2,5,6,3]))
        # Contradictory broader rule cannot relax the more specific prohibition.
        self.assertIn(NODES[5],self.route(1,3,4))
    def test_far_destination_refuses_to_invent_connector_roads(self):
        self.build_map(way(10,[1,2,3]))
        self.run_nav('route',self.map,self.routefile,*NODES[1],35,-119,1,ok=False)
        self.assertFalse(self.routefile.exists())
    def test_graph_is_bound_to_its_exact_visual_map(self):
        self.build_map(way(10,[1,2,3]));data=bytearray(self.map.read_bytes());data[20]^=1;self.map.write_bytes(data)
        self.route(1,3,1,ok=False)
    def test_route_data_crc_failure_is_detected_and_original_not_replaced(self):
        self.build_map(way(10,[1,2,3]));self.route(1,3);data=bytearray(self.routefile.read_bytes());data[90]^=1;self.routefile.write_bytes(data)
        self.run_nav('guide',self.routefile,self.map,*NODES[1],ok=False)
    def test_malformed_but_checksummed_route_coordinates_are_safe_to_reject(self):
        self.build_map(way(10,[1,2,3]));self.route(1,3);data=bytearray(self.routefile.read_bytes())
        struct.pack_into('<i',data,64,-2147483648);struct.pack_into('<I',data,20,0)
        struct.pack_into('<I',data,20,zlib.crc32(data));self.routefile.write_bytes(data)
        self.run_nav('guide',self.routefile,self.map,*NODES[1],ok=False)
    def test_sd_exhaustion_does_not_publish_an_incomplete_route(self):
        import resource,signal
        self.build_map(way(10,[1,2,3]));self.route(1,3);before=self.routefile.read_bytes()
        def constrain():
            signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(1024,1024))
        new=self.root/'full.route'
        p=subprocess.run([str(self.runner),'route',str(self.map),str(new),*map(str,NODES[1]),*map(str,NODES[3]),'1'],capture_output=True,preexec_fn=constrain)
        self.assertNotEqual(p.returncode,0);self.assertFalse(new.exists());self.assertEqual(before,self.routefile.read_bytes())
    def test_offroute_and_resume_validates_cache_without_network(self):
        self.build_map(way(10,[1,2,3]));self.route(1,3)
        values=list(map(int,self.run_nav('guide',self.routefile,self.map,34.055,-118.249).split('\t')[0].split()));self.assertEqual(values[4],1)
