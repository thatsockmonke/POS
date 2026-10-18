import hashlib
import json
import os
from pathlib import Path
import random
import resource
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class EditorNetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix="pos-editor-network-")
        cls.runner=Path(cls.build.name)/"runner"
        sdk=Path(os.environ.get("IDF_PATH","/workspace/pos-cloud/esp-idf-v5.5.5"));cjson=sdk/"components/json/cJSON"
        sources=[ROOT/"tests/editor_network_runner.c",ROOT/"components/pos_install/pos_hash.c",cjson/"cJSON.c",*sorted((ROOT/"components/pos_core").glob("*.c"))]
        command=["gcc","-std=c11","-D_POSIX_C_SOURCE=200809L","-O1","-Wall","-Wextra","-Werror","-fsanitize=undefined","-fno-sanitize-recover=all"]
        for component in ("pos_core","pos_install"):command.extend(["-I",str(ROOT/"components"/component/"include")])
        subprocess.run(command+["-I",str(cjson),*map(str,sources),"-lcrypto","-lm","-o",str(cls.runner)],check=True)

    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="pos-editor-test-");self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);(self.root/"Documents").mkdir();(self.root/"Downloads").mkdir()
        self.serial=0
    def run_tool(self,mode,*args,expected=0,limit=None):
        def constrain():resource.setrlimit(resource.RLIMIT_AS,(limit,limit))
        result=subprocess.run([str(self.runner),mode,str(self.root),*map(str,args)],capture_output=True,preexec_fn=constrain if limit else None)
        self.assertEqual(result.returncode,expected,result.stderr.decode(errors="replace"));return result.stdout
    def ops(self,operations):
        self.serial+=1;p=self.root/f"ops-{self.serial}.json";p.write_text(json.dumps(operations));return p
    def open(self,data=b"",operations=()):
        path=self.root/"Documents/notes.txt";path.write_bytes(data)
        folder=self.run_tool("open","Documents/notes.txt",self.ops(operations)).decode().strip();return folder
    def read(self,folder,length):
        return b"".join(self.run_tool("read",folder,i,min(8192,length-i)) for i in range(0,length,8192))
    def test_symlinked_undo_workspace_file_cannot_overwrite_a_user_document(self):
        folder=self.open(b"original")
        victim=self.root/"Documents/victim.txt";victim.write_bytes(b"preserve this")
        (self.root/folder/"undo.0.csv").symlink_to(victim)
        self.run_tool("edit",folder,self.ops([dict(at=0,remove=0,text="edit")]),expected=1)
        self.assertEqual(victim.read_bytes(),b"preserve this")
        self.assertEqual((self.root/"Documents/notes.txt").read_bytes(),b"original")

    def test_undo_redo_and_new_edit_after_undo_follow_an_independent_model(self):
        ops=[dict(at=3,remove=2,text="XYZ"),dict(at=0,remove=0,text="prefix "),dict(action="undo"),dict(action="redo"),dict(action="undo"),dict(action="undo"),dict(action="redo"),dict(at=0,remove=0,text="new ")]
        folder=self.open(b"abcdefghij",ops)
        self.assertEqual(self.read(folder,15),b"new abcXYZfghij")
        self.assertEqual((self.root/"Documents/notes.txt").read_bytes(),b"abcdefghij")
        self.run_tool("discard",folder)
        self.assertFalse((self.root/folder).exists())
    def test_undo_ring_retains_32_edits_and_redo_restores_them(self):
        edits=[dict(at=i,remove=0,text=chr(65+i%26)) for i in range(40)]
        folder=self.open(b"",edits+[dict(action="undo")]*32+[dict(action="redo")]*5)
        expected=bytes(65+i%26 for i in range(13))
        self.assertEqual(self.read(folder,len(expected)),expected)
    def test_large_selection_copy_cut_paste_and_undo_use_sd_not_document_ram(self):
        data=b"line of original words\n"*100000
        start,end=123,1500000
        copied=data[start:end]
        ops=[dict(action="copy",start=start,end=end),dict(at=start,remove=end-start,text=""),dict(action="undo"),dict(action="redo"),dict(action="paste",at=0,remove=0)]
        folder=self.open(data,ops)
        model=copied+data[:start]+data[end:]
        self.assertEqual(self.read(folder,len(model)),model)
        self.assertEqual((self.root/folder/"clipboard.txt").read_bytes(),copied)

    def test_random_edits_match_an_independent_byte_string_model_and_coalesce(self):
        rng=random.Random(107);original=b"start\n"*250;model=original;operations=[]
        for _ in range(160):
            at=rng.randrange(len(model)+1);remove=rng.randrange(min(12,len(model)-at)+1);text=rng.choice(["abc","","\nnew line\n","Stevie","é"])
            operations.append(dict(at=at,remove=remove,text=text));model=model[:at]+text.encode()+model[at+remove:]
        folder=self.open(original,operations)
        self.assertEqual(self.read(folder,len(model)),model)
        self.assertEqual((self.root/"Documents/notes.txt").read_bytes(),original)
        self.run_tool("save",folder,"Documents/notes.txt")
        self.assertEqual((self.root/"Documents/notes.txt").read_bytes(),model)
        self.assertEqual((self.root/"Documents/notes.txt.previous-0").read_bytes(),original)
        self.run_tool("edit",folder,self.ops([dict(at=len(model),remove=0,text="again")]))
        self.run_tool("save",folder,"Documents/notes.txt")
        self.assertEqual((self.root/"Documents/notes.txt").read_bytes(),model+b"again")
    def test_large_document_uses_bounded_memory_and_edits_middle_and_end(self):
        path=self.root/"Documents/large.txt"
        block=b"0123456789abcdef\n"*4096
        with path.open("wb") as f:
            for _ in range(1000):f.write(block)
        length=path.stat().st_size;middle=length//2
        folder=self.run_tool("open","Documents/large.txt",self.ops([dict(at=middle,remove=7,text="EDITED"),dict(at=length-1,remove=0,text="END")]),limit=48*1024*1024).decode().strip()
        self.assertEqual(self.run_tool("read",folder,middle,6),b"EDITED")
        self.assertEqual(self.run_tool("read",folder,length-1,3),b"END")
        self.assertEqual(path.stat().st_size,length)
    def test_find_crosses_buffer_boundaries_and_go_to_line_streams(self):
        data=b"a"*4094+b"needle\n"+b"line\n"*3000
        folder=self.open(data)
        self.assertEqual(int(self.run_tool("find",folder,"needle",0)),4094)
        self.assertEqual(int(self.run_tool("line",folder,3001)),len(data)-5)
        self.run_tool("find",folder,"missing",0,expected=1)
    def test_interrupted_edit_recovers_previous_complete_index(self):
        folder=self.open(b"original",[dict(at=0,remove=0,text="new ")])
        directory=self.root/folder
        states=list(directory.glob("state.*.json"))
        newest=max(states,key=lambda p:json.loads(json.loads(p.read_text())["payload"])["generation"])
        newest.write_text("torn checkpoint")
        self.assertEqual(self.read(folder,8),b"original")
        self.assertEqual((self.root/"Documents/notes.txt").read_bytes(),b"original")
    def test_changed_original_and_save_as_collision_preserve_every_copy(self):
        folder=self.open(b"original",[dict(at=0,remove=0,text="edit ")])
        (self.root/"Documents/notes.txt").write_bytes(b"external change")
        self.run_tool("save",folder,"Documents/notes.txt",expected=1)
        (self.root/"Documents/other.txt").write_bytes(b"keep")
        self.run_tool("save",folder,"Documents/other.txt",expected=1)
        self.run_tool("save",folder,"Documents/recovered.txt")
        self.assertEqual((self.root/"Documents/recovered.txt").read_bytes(),b"edit original")
        self.assertEqual((self.root/"Documents/notes.txt").read_bytes(),b"external change")
    def test_corrupted_working_text_is_not_published_and_original_is_preserved(self):
        folder=self.open(b"original",[dict(at=0,remove=0,text="new ")])
        directory=self.root/folder
        (directory/"base.txt").write_bytes(b"CORRUPT!")
        self.run_tool("save",folder,"Documents/notes.txt",expected=1)
        self.assertEqual((self.root/"Documents/notes.txt").read_bytes(),b"original")

    def test_working_drafts_are_user_owned_and_recoverable_on_another_root(self):
        import shutil
        folder=self.open(b"original",[dict(at=8,remove=0,text=" edited")])
        self.assertTrue(folder.startswith("POS/Users/Owner/EditorDrafts/"))
        other=self.root/"restored";shutil.copytree(self.root/"POS",other/"POS")
        command=[str(self.runner),"read",str(other),folder,"0","100"]
        result=subprocess.run(command,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout,b"original edited")

    def test_rejects_system_and_traversal_paths_and_binary_documents(self):
        for name in ("../outside","POS/System/test.txt","POS/Users/settings.0.json"):
            self.run_tool("open",name,self.ops([]),expected=1)
        (self.root/"Documents/binary").write_bytes(b"hello\0binary")
        self.run_tool("open","Documents/binary",self.ops([]),expected=1)
    def test_vault_uses_standard_pbkdf2_aes_gcm_and_rejects_tampering(self):
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        self.run_tool("vault")
        blob=(self.root/"vault.bin").read_bytes();self.assertEqual(len(blob),447)
        self.assertNotIn(b"secret-network-password",blob)
        key=hashlib.pbkdf2_hmac("sha256",b"correct horse battery staple",blob[8:24],100000,32)
        plain=AESGCM(key).decrypt(blob[24:36],blob[38:],blob[:38])
        self.assertEqual(plain[0],4);self.assertEqual(plain[1:10],b"Network 0")
        self.assertEqual(plain[34:57],b"secret-network-password")
    def test_https_and_transfer_path_policy(self):self.run_tool("policy")
    def test_mesh_wifi_scan_groups_same_ssid_keeps_strongest_and_separates_security(self):
        self.run_tool('mesh-wifi-scan')
    def receive(self,name,data,expected_size="unknown",sha="-",fail=-1,cancel=-1,expected=0):
        source=self.root/"incoming.bin";source.write_bytes(data)
        return self.run_tool("receive","Downloads/"+name,source,expected_size,sha,fail,cancel,expected=expected)
    def test_receive_streams_verifies_sha_and_never_overwrites(self):
        data=b"packet"*10000;sha=hashlib.sha256(data).hexdigest()
        self.receive("file.bin",data,len(data),sha)
        self.assertEqual((self.root/"Downloads/file.bin").read_bytes(),data)
        self.receive("file.bin",b"replacement",expected=1)
        self.assertEqual((self.root/"Downloads/file.bin").read_bytes(),data)
    def test_truncation_wrong_hash_cancellation_and_reader_failure_remain_partial(self):
        data=b"x"*15000
        cases=[dict(expected_size=20000),dict(sha="0"*64),dict(cancel=4096),dict(fail=4096)]
        for i,options in enumerate(cases):
            name=f"case-{i}.bin";self.receive(name,data,expected=1,**options)
            self.assertFalse((self.root/"Downloads"/name).exists())
            self.assertTrue(list((self.root/"Downloads").glob(name+".part-*")))
        self.receive("case-0.bin",data,len(data))
        self.assertEqual((self.root/"Downloads/case-0.bin").read_bytes(),data)

    def test_editor_delete_moves_original_to_trash_and_discards_working_draft_only_after_success(self):
        folder=self.open(b'original document', [dict(at=0,remove=0,text='unsaved ')])
        self.run_tool('delete',folder)
        self.assertFalse((self.root/'Documents/notes.txt').exists())
        self.assertFalse((self.root/folder).exists())
        self.assertTrue(any(p.is_file() and p.read_bytes()==b'original document' for p in (self.root/'POS/Users/Owner/Trash').rglob('*')))
    def test_editor_delete_refuses_changed_original_and_protected_paths(self):
        folder=self.open(b'original')
        path=self.root/'Documents/notes.txt';path.write_bytes(b'changed outside editor')
        self.run_tool('delete',folder,expected=1)
        self.assertEqual(path.read_bytes(),b'changed outside editor')
        self.assertTrue((self.root/folder).is_dir())
        self.run_tool('delete-protected',folder,expected=1)
    def test_editor_nearby_cursor_reads_use_bounded_cache_without_reopening_sd_index(self):
        folder=self.open(b'abcdefgh'*4096)
        self.run_tool('cached-reads',folder)
