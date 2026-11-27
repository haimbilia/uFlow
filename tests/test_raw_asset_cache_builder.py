import hashlib,json,subprocess,sys,tempfile,unittest
from pathlib import Path
TOOL=Path(__file__).resolve().parents[1]/'tools/build-raw-asset-cache.py'
class CacheBuildTest(unittest.TestCase):
 def test_verified_overlay_and_small_buckets(self):
  with tempfile.TemporaryDirectory() as tmp:
   sd=Path(tmp);base=sd/'wiiu/raw-games/Test';up=base/'updates/latest'
   for root,title in [(base,'00050000101c9400'),(up,'0005000e101c9400')]:
    (root/'code').mkdir(parents=True);(root/'code/app.xml').write_text('<app><title_id>'+title+'</title_id></app>');(root/'content/Model').mkdir(parents=True)
   for n in range(130):(base/f'content/Model/{n:03}.bin').write_bytes(bytes([n]))
   (up/'content/Model/000.bin').write_bytes(b'update wins')
   (base/'content/Small').mkdir();(base/'content/Small/only.bin').write_bytes(b'fallback')
   before={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in base.rglob('*') if p.is_file()}
   run=subprocess.run([sys.executable,str(TOOL),'--sd',str(sd),'--game','wiiu/raw-games/Test','--min-directory-files','64','--build'],check=True,capture_output=True,text=True)
   done=json.loads(run.stdout.splitlines()[-1]);cache=Path(done['cache']);self.assertEqual(done['files'],130)
   report=json.loads((cache/'verification.json').read_text());self.assertTrue((cache/'manifest.ready').is_file())
   for e in report['files']:
    self.assertEqual(hashlib.sha256((cache/e['cache']).read_bytes()).hexdigest(),e['sha256'])
    self.assertEqual((cache/e['cache']).read_bytes(),(sd/e['source']).read_bytes())
   self.assertEqual((cache/report['files'][0]['cache']).read_bytes(),b'update wins')
   self.assertTrue(all(len(list(d.iterdir()))<=64 for d in (cache/'data').rglob('*') if d.is_dir()))
   self.assertEqual(before,{p:hashlib.sha256(p.read_bytes()).hexdigest() for p in before})
   self.assertNotIn('small/only.bin',(cache/'manifest.ready').read_text())
if __name__=='__main__':unittest.main()
