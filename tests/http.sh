#!/bin/bash
set -euo pipefail
python3 - <<'PY'
import concurrent.futures, contextlib, http.client, os, pathlib, socket, subprocess, tempfile, threading, time
binroot=pathlib.Path('hw-http').resolve()
def port():
    with socket.socket() as s: s.bind(('127.0.0.1',0)); return s.getsockname()[1]
@contextlib.contextmanager
def server(name,args):
    p=port(); log=open(f'evidence/{name}-{p}.txt','w')
    proc=subprocess.Popen([str(binroot/name),*args,'--port',str(p)],stdout=log,stderr=log,start_new_session=True)
    try:
        for _ in range(100):
            try:
                with socket.create_connection(('127.0.0.1',p),.1): break
            except OSError: time.sleep(.03)
        else: raise AssertionError('server not listening')
        yield p,proc
        assert proc.poll() is None
    finally:
        os.killpg(proc.pid,15); proc.wait(timeout=5); log.close()
def get(p,path):
    c=http.client.HTTPConnection('127.0.0.1',p,timeout=10); c.request('GET',path)
    r=c.getresponse(); data=r.read(); headers=dict(r.getheaders()); status=r.status;c.close();return status,headers,data
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp); payload=bytes(range(256))*8192
    (root/'blob').write_bytes(payload);(root/'empty').write_bytes(b'');(root/'index.html').write_text('root-index')
    (root/'sub').mkdir();(root/'sub'/'index.html').write_text('sub-index')
    (root/'listing').mkdir();(root/'listing'/'child.txt').write_text('child')
    for name in ['httpserver','forkserver','threadserver','poolserver']:
        with server(name,['--files',tmp,'--num-threads','3']) as (p,proc):
            for path,expected in [('/','root-index'),('/sub','sub-index'),('/sub/','sub-index'),('/empty','')]:
                status,h,data=get(p,path);assert status==200 and data==expected.encode();assert int(h['Content-Length'])==len(data)
            status,h,data=get(p,'/blob');assert status==200 and data==payload and int(h['Content-Length'])==len(payload)
            assert get(p,'/absent')[0]==404
            assert get(p,'/../secret')[0]==403
            status,h,data=get(p,'/listing'); assert status==200 and b'child.txt' in data and b'..' in data
            with socket.create_connection(('127.0.0.1',p)) as s:
                for chunk in [b'G',b'ET /sub ',b'HTTP/1.0\r\n',b'Host: local\r\n\r\n']:
                    s.sendall(chunk);time.sleep(.02)
                r=http.client.HTTPResponse(s);r.begin();assert r.read()==b'sub-index'
            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                assert all(x==b'child' for x in pool.map(lambda _:get(p,'/listing/child.txt')[2],range(60)))
            if name!='httpserver':
                stalled=socket.create_connection(('127.0.0.1',p));stalled.sendall(b'GET ')
                assert get(p,'/sub')[2]==b'sub-index';stalled.close()
            if name=='poolserver':
                assert len(list(pathlib.Path(f'/proc/{proc.pid}/task').iterdir()))==4
            out=subprocess.check_output(['ab','-n','500','-c','10',f'http://127.0.0.1:{p}/'],stderr=subprocess.STDOUT)
            assert b'Failed requests:        0' in out,out
            pathlib.Path(f'evidence/{name}-ab.txt').write_bytes(out)
        print(name,'files/fragmentation/concurrency/ab passed',flush=True)
    # Upstream initiates communication; repeated frames require simultaneous relay.
    for name in ['httpserver','forkserver','threadserver','poolserver']:
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();up=listener.getsockname()[1]
        errors=[]
        def upstream():
            try:
                # readiness probe consumes one connection, then real test connection
                while True:
                    conn,_=listener.accept()
                    with conn:
                        conn.sendall(b'hello')
                        for i in range(3):
                            data=b''
                            while len(data)<100000:
                                chunk=conn.recv(100000-len(data))
                                if not chunk:break
                                data+=chunk
                            if not data:break
                            assert data==bytes([i])*100000
                            conn.sendall(data)
                        else:return
            except Exception as e: errors.append(repr(e))
        worker=threading.Thread(target=upstream,daemon=True);worker.start()
        with server(name,['--proxy',f'127.0.0.1:{up}','--num-threads','3']) as (p,proc):
            with socket.create_connection(('127.0.0.1',p),timeout=10) as s:
                def read(n):
                    data=b''
                    while len(data)<n:
                        part=s.recv(n-len(data));assert part;data+=part
                    return data
                assert read(5)==b'hello'
                for i in range(3):
                    frame=bytes([i])*100000;s.sendall(frame);assert read(len(frame))==frame
            worker.join(10);assert not worker.is_alive() and not errors,errors
        listener.close(); print(name,'upstream-first repeated binary proxy passed',flush=True)
print('All four server models passed file and bidirectional proxy checks')
PY
