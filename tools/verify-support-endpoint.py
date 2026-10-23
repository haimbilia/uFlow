"""Exercise the live opt-in intake using a synthetic report; retain its ID for cleanup."""
import json
from pathlib import Path
import urllib.request
import urllib.error
url = 'https://karotech.io/wp-json/karo-uflow/v1/reports'
def request(method, body=None, content_type='application/json'):
    req=urllib.request.Request(url, data=body, method=method, headers={'Content-Type':content_type})
    try:
        with urllib.request.urlopen(req,timeout=30) as r:return r.status,r.read()
    except urllib.error.HTTPError as e:return e.code,e.read()
assert request('GET')[0] == 404
assert request('POST', b'{}')[0] == 400
assert request('POST', b'{}', 'text/plain')[0] == 415
assert request('POST', json.dumps('x'*262145).encode())[0] == 413
payload={'schema':1,'build':2026102302,'redacted':True,'games':0,'libraries':0,'media':0,
         'settings':'','events':'Synthetic ingress verification; safe to delete.','logs':{}}
status,body=request('POST',json.dumps(payload).encode())
assert status==201,(status,body)
report=json.loads(body)
assert report['report_id'].startswith('UF-') and len(report['report_id'])==15
Path('artifacts/support/endpoint-fixture.json').write_text(json.dumps(report),encoding='utf-8')
print('PASS HTTPS POST, schema/content type/size checks and no public GET. Fixture:',report['report_id'])
