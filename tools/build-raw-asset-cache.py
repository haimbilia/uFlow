"""Build a verified read-only cache for large raw-game content directories.
Run without --build to inspect space/counts. Rebuild after modifying source assets.
Original content, code, updates, metadata and saves are never changed.
"""
import argparse,hashlib,json,os,shutil,time
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path

def key(s):
    return ''.join(chr(ord(c)+32) if 'A'<=c<='Z' else c for c in s)
def fingerprint(data):
    h=14695981039346656037
    for c in data:h=((h^c)*1099511628211)&0xffffffffffffffff
    return f'{h:016x}'
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--sd',required=True);a.add_argument('--game',required=True);a.add_argument('--update',default='updates/latest');a.add_argument('--min-directory-files',type=int,default=256);a.add_argument('--build',action='store_true');args=a.parse_args()
    sd=Path(args.sd).resolve();base=(sd/args.game).resolve();update=(base/args.update).resolve() if args.update else None
    if not base.is_relative_to(sd) or (update and not update.is_relative_to(base)):raise ValueError('Source outside SD/game root')
    import xml.etree.ElementTree as ET
    base_xml=(base/'code/app.xml').read_bytes();title=ET.fromstring(base_xml).findtext('title_id').strip().lower()
    if len(title)!=16 or not title.startswith('00050000'):raise ValueError('Expected base game title ID')
    update_xml=(update/'code/app.xml').read_bytes() if update else b''
    if update and ET.fromstring(update_xml).findtext('title_id').strip().lower()!='0005000e'+title[8:]:raise ValueError('Update title does not match base')
    base_rel=(base/'content').relative_to(sd).as_posix();update_rel=(update/'content').relative_to(sd).as_posix() if update else ''
    identity=fingerprint((base_rel+'\n'+update_rel+'\n').encode()+base_xml+b'\n'+update_xml)
    cache=(sd/'wiiu/cache/raw-assets'/title/identity).resolve()
    if not cache.is_relative_to(sd/'wiiu/cache/raw-assets'):raise ValueError('Invalid cache target')
    files={}
    for origin in ([base/'content',update/'content'] if update else [base/'content']):
        if not origin.is_dir():raise ValueError('Missing content root '+str(origin))
        local=set()
        for parent,dirs,names in os.walk(origin):
            for name in names:
                p=Path(parent)/name;logical=key(p.relative_to(origin).as_posix())
                if logical in local:raise ValueError('Case-colliding source names: '+logical)
                local.add(logical);files[logical]=p
    counts={}
    for name in files:parent=name.rpartition('/')[0];counts[parent]=counts.get(parent,0)+1
    selected=sorted((n,p) for n,p in files.items() if counts[n.rpartition('/')[0]]>=args.min_directory_files)
    if len(selected)>262144:raise ValueError('Cache capacity exceeded')
    total=sum(p.stat().st_size for _,p in selected);free=shutil.disk_usage(sd).free
    print(json.dumps(dict(cache=str(cache),identity=identity,files=len(selected),bytes=total,free=free,large_directories={k:v for k,v in counts.items() if v>=args.min_directory_files})),flush=True)
    if not args.build:return
    if free<total+256*1024*1024:raise ValueError('Insufficient space for verified cache')
    cache.mkdir(parents=True,exist_ok=True);manifest=cache/'manifest.ready'
    if manifest.exists():manifest.unlink() # Disable only this generated cache while rebuilding.
    records=[None]*len(selected);lines=['UFLOW-ASSET-CACHE-1',identity,str(len(selected))];start=time.monotonic();last=start;done=0;finished=0
    def copy_one(slot,item):
        logical,src=item
        if any(c in logical for c in '\t\r\n') or len(logical.encode())>768:raise ValueError('Unsupported cache key')
        rel=f'data/{slot>>12:02x}/{(slot>>6)&63:02x}/{slot&63:02x}.bin';dst=cache/rel;dst.parent.mkdir(parents=True,exist_ok=True)
        before=src.stat();expected=None
        if dst.exists() and dst.stat().st_size==before.st_size:
            expected=digest(src)
            if digest(dst)!=expected:expected=None
        if expected is None:
            h=hashlib.sha256();tmp=dst.with_suffix('.part')
            with src.open('rb') as inp,tmp.open('wb') as out:
                for data in iter(lambda:inp.read(1024*1024),b''):h.update(data);out.write(data)
            os.replace(tmp,dst);expected=h.hexdigest()
            if digest(dst)!=expected:raise ValueError('Cache verification failed: '+logical)
        after=src.stat()
        if before.st_size!=after.st_size or before.st_mtime_ns!=after.st_mtime_ns:raise ValueError('Source changed during copy')
        return dict(logical=logical,source=src.relative_to(sd).as_posix(),cache=rel,size=after.st_size,sha256=expected)
    pool=ThreadPoolExecutor(max_workers=4)
    try:
        futures={pool.submit(copy_one,i,item):i for i,item in enumerate(selected)}
        for future in as_completed(futures):
            record=future.result();records[futures[future]]=record;done+=record['size'];finished+=1;now=time.monotonic()
            if now-last>=10:print(f'Verified {finished}/{len(selected)} files, {done/1048576:.1f}/{total/1048576:.1f} MiB, {now-start:.0f}s',flush=True);last=now
    except BaseException:
        pool.shutdown(wait=True,cancel_futures=True);raise
    else:pool.shutdown(wait=True)
    lines.extend(f'{logical}\t{slot:06x}' for slot,(logical,_) in enumerate(selected))
    (cache/'verification.json').write_text(json.dumps(dict(identity=identity,files=records),indent=2),encoding='utf-8')
    lines.append('END');ready=cache/'manifest.part';ready.write_bytes(('\n'.join(lines)+'\n').encode('utf-8'));os.replace(ready,manifest)
    print(json.dumps(dict(complete=True,cache=str(cache),files=len(selected),bytes=done,seconds=time.monotonic()-start)),flush=True)
if __name__=='__main__':main()
