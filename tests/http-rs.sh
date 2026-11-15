#!/bin/bash
set -euo pipefail
python3 - <<'PY'
import concurrent.futures, http.client, os, pathlib, socket, subprocess, tempfile, time
binary=pathlib.Path('hw-http-rs/target/debug/http_server_rs').resolve()
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp);(root/'index.html').write_bytes(b'index');(root/'stats').write_bytes(b'should-be-overridden')
    payload=bytes(range(256))*8192;(root/'blob').write_bytes(payload)
    (root/'sub').mkdir();(root/'sub'/'index.html').write_bytes(b'sub-index')
    (root/'listing').mkdir();(root/'listing'/'child').write_bytes(b'child')
    (root/'empty').write_bytes(b'')
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    log=open('hw-http-rs/evidence/server.txt','w')
    proc=subprocess.Popen([str(binary),'--files',tmp,'--port',str(port),'--num-threads','4'],stdout=log,stderr=log)
    def get(path):
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=10);c.request('GET',path);r=c.getresponse();data=r.read();out=(r.status,dict(r.getheaders()),data);c.close();return out
    try:
        for _ in range(100):
            try:get('/stats');break
            except OSError:time.sleep(.05)
        assert get('/stats')[2]==b''
        for path,data in [('/',b'index'),('/sub',b'sub-index'),('/sub/',b'sub-index'),('/blob',payload),('/empty',b'')]:
            status,headers,body=get(path);assert status==200 and body==data
            assert int(headers['Content-Length'])==len(data)
        assert get('/missing')[0]==404
        assert get('/../blocked')[0]==403
        assert b'child' in get('/listing')[2]
        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
            assert all(body==payload for body in pool.map(lambda _:get('/blob')[2],range(100)))
        status,headers,stats=get('/stats');assert status==200 and headers['Content-Type']=='text/plain'
        assert stats==b'OK: 106\nForbidden: 1\nNot Found: 1\n',stats
        assert get('/stats')[2]==stats
        stalled=socket.create_connection(('127.0.0.1',port));stalled.sendall(b'GET ')
        assert get('/sub')[2]==b'sub-index';stalled.close()
        print('files/index/listing/2MiB/100 concurrent reads and exact non-self-counting stats passed')
    finally:proc.terminate();proc.wait(timeout=5);log.close()
PY
