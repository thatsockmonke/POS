"""User-managed P-256 update signing. Requires the openssl command, no Python packages."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.parse import urlsplit

# Only the transition release and older packages carry the obsolete field.
TRANSITION_RELEASE = '0.6.0-dev'
LEGACY_MINIMUM_VERSION = '0.0.0'

def version(value: str):
    m=re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*))?',value)
    if not m or len(value)>31: raise ValueError('Use a semantic version: X.Y.Z or X.Y.Z-prerelease')
    parts=m.group(4).split('.') if m.group(4) else []
    if any(p.isdigit() and len(p)>1 and p[0]=='0' for p in parts):raise ValueError('Numeric prerelease identifiers cannot have leading zeroes')
    numeric=tuple(map(int,m.group(1,2,3)))
    if any(n>0xffffffff for n in numeric):raise ValueError('Version component exceeds the device limit')
    return numeric,parts


def compare(a: str,b: str):
    x,p=version(a);y,q=version(b)
    if x!=y:return (x>y)-(x<y)
    if not p or not q:return (not p)-(not q)
    for left,right in zip(p,q):
        if left==right:continue
        if left.isdigit()!=right.isdigit():return -1 if left.isdigit() else 1
        if left.isdigit():return (int(left)>int(right))-(int(left)<int(right))
        return (left>right)-(left<right)
    return (len(p)>len(q))-(len(p)<len(q))


def keygen(private: Path,public: Path):
    if private.exists() or private.is_symlink() or public.exists() or public.is_symlink() or private.resolve()==public.resolve():raise ValueError('Signing key paths must be different and new; existing keys are never replaced')
    private.parent.mkdir(parents=True,exist_ok=True);public.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pos-update-key-') as work:
        generated=Path(work)/'private.pem'
        subprocess.run(['openssl','genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-256','-out',str(generated)],check=True,capture_output=True)
        pub=subprocess.run(['openssl','pkey','-in',str(generated),'-pubout'],check=True,capture_output=True).stdout
        der=subprocess.run(['openssl','pkey','-pubin','-outform','DER'],input=pub,check=True,capture_output=True).stdout
        fd=os.open(private,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        with os.fdopen(fd,'wb') as stream:stream.write(generated.read_bytes());stream.flush();os.fsync(stream.fileno())
        with public.open('xb') as stream:stream.write(pub);stream.flush();os.fsync(stream.fileno())
    return hashlib.sha256(der).hexdigest()


def validate_package(package,api):
    if not isinstance(package,dict) or set(package)!={'format','model','version','firmware','files'} or type(package['format']) is not int or package['format']!=1 or package['model']!=api.MODEL or not isinstance(package['version'],str):raise ValueError('Unsupported installed manifest')
    version(package['version'])
    fw=package['firmware']
    if not isinstance(fw,dict) or set(fw)!={'path','size','sha256'} or fw['path']!='firmware.bin' or type(fw['size']) is not int or not 288<=fw['size']<=api.FW_LIMIT or not isinstance(fw['sha256'],str) or not re.fullmatch('[0-9a-f]{64}',fw['sha256']):raise ValueError('Invalid firmware record')
    if not isinstance(package['files'],list) or not 1<=len(package['files'])<=api.MAX_FILES:raise ValueError('Invalid System file list')
    paths=[]
    for r in package['files']:
        if not isinstance(r,dict) or set(r)!={'path','size','sha256'} or not isinstance(r['path'],str) or not api.safe_path(r['path']) or type(r['size']) is not int or not 0<=r['size']<=0xffffffff or not isinstance(r['sha256'],str) or not re.fullmatch('[0-9a-f]{64}',r['sha256']):raise ValueError('Invalid System file record')
        p=r['path'].lower()
        if any(p==x or p.startswith(x+'/') or x.startswith(p+'/') for x in paths):raise ValueError('System paths conflict on FAT32')
        paths.append(p)


def make_update(args,api):
    version(args.release)
    if not 1<=args.sequence<=0xffffffff:raise ValueError('Sequence must be 1..4294967295 and increase for every published update')
    url=urlsplit(args.base_url)
    if url.scheme!='https' or not url.hostname or url.username or url.password or url.query or url.fragment or any(c.isspace() for c in args.base_url) or len(args.base_url)>350:raise ValueError('base-url must be a complete HTTPS directory URL without credentials, query or fragment')
    output=args.output.absolute()
    if output.exists() or output.is_symlink():raise ValueError('Output exists; use a new update directory')
    if output.resolve().is_relative_to(args.payload.resolve()):raise ValueError('Output must be outside the payload')
    output.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='.pos-update-',dir=output.parent))
    try:
        if args.kind=='firmware':
            if not args.firmware:raise ValueError('Firmware updates require --firmware')
            package_dir=api.make_package(args.firmware,args.payload,stage/'install')
            package=json.loads((package_dir/'manifest.pos').read_text())
            if package['version']!=args.release:raise ValueError('Release must match the firmware image version')
            shutil.move(str(package_dir/'firmware.bin'),stage/'firmware.bin');shutil.move(str(package_dir/'payload'),stage/'payload');shutil.rmtree(stage/'install')
        else:
            if not args.installed_manifest:raise ValueError('Asset updates require the exact --installed-manifest for their target firmware')
            package=json.loads(args.installed_manifest.read_text())
            validate_package(package,api)
            records={record['path'].lower():record for record in package['files']}
            count=0
            if args.payload.is_symlink() or not args.payload.is_dir():raise ValueError('Payload must be a real directory')
            for directory,dirs,names in os.walk(args.payload,followlinks=False):
                for name in dirs+names:
                    if (Path(directory)/name).is_symlink():raise ValueError('Payload symlinks are refused')
                for name in names:
                    source=Path(directory)/name;relative=source.relative_to(args.payload).as_posix()
                    if not source.is_file() or not api.safe_path(relative) or source.stat().st_size>0xffffffff:raise ValueError('Unsafe asset path or file size')
                    old=records.get(relative.lower())
                    if old and old['path']!=relative:raise ValueError('Asset path changes case on FAT32')
                    dest=stage/'payload'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
                    digest=api.sha256(dest)
                    if digest!=api.sha256(source):raise ValueError('Asset changed while packaging')
                    records[relative.lower()]={'path':relative,'size':dest.stat().st_size,'sha256':digest};count+=1
            if not count:raise ValueError('Include at least one changed asset')
            package['files']=sorted(records.values(),key=lambda r:r['path'])
        validate_package(package,api)
        payload={'format':1,'model':api.MODEL,'release':args.release,'sequence':args.sequence,'kind':args.kind,'base_url':args.base_url,'package':package}
        if compare(args.release,TRANSITION_RELEASE)<=0:
            payload['minimum_version']=LEGACY_MINIMUM_VERSION
        body=json.dumps(payload,separators=(',',':'),ensure_ascii=True)
        signature=subprocess.run(['openssl','dgst','-sha256','-sign',str(args.private_key)],input=body.encode(),check=True,capture_output=True).stdout
        public=subprocess.run(['openssl','pkey','-in',str(args.private_key),'-pubout'],check=True,capture_output=True).stdout
        key_info=subprocess.run(['openssl','pkey','-pubin','-text','-noout'],input=public,check=True,capture_output=True).stdout
        if b'ASN1 OID: prime256v1' not in key_info:raise ValueError('Signing key must be P-256')
        envelope=json.dumps({'payload':body,'signature':signature.hex()},separators=(',',':')).encode()+b'\n'
        if len(envelope)>32768:raise ValueError('Signed manifest exceeds the on-device 32-KiB limit')
        (stage/'manifest.json').write_bytes(envelope)
        (stage/'public-key.pem').write_bytes(public)
        sums=''.join(f'{api.sha256(p)}  {p.relative_to(stage).as_posix()}\n' for p in sorted(stage.rglob('*')) if p.is_file())
        (stage/'SHA256SUMS').write_text(sums)
        stage.rename(output)
    except BaseException:
        shutil.rmtree(stage);raise
    return output
