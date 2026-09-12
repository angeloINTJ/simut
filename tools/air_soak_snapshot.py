#!/usr/bin/env python3
"""Snapshot the Air bench in M0: status, today's full history (sealed+open), log count, tel target.
Run at soak start and end; diff the two JSONs. Read-only. Usage: air_soak_snapshot.py <out.json>"""
import os, sys, json, time
sys.path.insert(0,'/home/angelo/Documentos/simut/tools')
from air_test_suite import Web, h5_epochs
w=Web('192.168.3.24', timeout=20); w.login(os.environ.get('SIMUT_WEB_USER','admin'), os.environ['SIMUT_WEB_PASS'])
st=w.get('/api/status').json().get('sys',{})
cfg=w.get('/api/config').json()
def find(d, keys):
    out={}
    def walk(x, path=''):
        if isinstance(x,dict):
            for k,v in x.items():
                if any(s in k.lower() for s in keys) and not isinstance(v,(dict,list)): out[path+k]=v
                walk(v, path+k+'.')
    walk(d); return out
tel=find(cfg, ('t_serv','t_host','t_url','tel_serv','server','host','url','t_port','t_transport','t_int','t_bat'))
# history: today (and yesterday, in case the run crosses midnight)
def epochs(day):
    try:
        blob=w.download(f'/history/{day}.h5')
        if not blob: return []
        return h5_epochs(blob, 60)
    except Exception as e:
        print('  (download', day, 'failed:', type(e).__name__, str(e)[:60], ')'); return []
def opened():
    try:
        raw=w.open_block()
        if not raw: return []
        return h5_epochs(raw, 60)
    except Exception as e:
        print('  (open block failed:', type(e).__name__, str(e)[:60], ')'); return []
today=time.strftime('%Y%m%d'); yday=time.strftime('%Y%m%d', time.localtime(time.time()-86400))
sealed_t=epochs(today); sealed_y=epochs(yday); op=opened()
# log entries: /api/logs is 12 B/record (binary)
try:
    lr=w.s.get(w.base+'/api/logs', timeout=30); nlog=len(lr.content)//12 if lr.status_code==200 else None
except Exception: nlog=None
snap={'ts':time.time(),'local':time.strftime('%Y-%m-%d %H:%M:%S'),
      'version':w.get('/api/perms').json().get('version'),
      'fs_u':st.get('fs_u'),'heap_f':st.get('heap_f'),'heap_lb':st.get('heap_lb'),'pending':st.get('pending'),
      'hi':st.get('hi'),'tel':st.get('tel'),'uptime':st.get('uptime'),
      'records_today':len(sealed_t),'records_yday':len(sealed_y),'records_open':len(op),
      'last_epoch':max(sealed_t+op) if (sealed_t or op) else None,
      'log_entries':nlog,'tel_cfg':tel}
json.dump(snap, open(sys.argv[1],'w'), indent=1)
print(json.dumps(snap, ensure_ascii=False))
