#!/usr/bin/env python3
"""Captura controles por API pública con catálogo aislado, sin tocar sesiones abiertas."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
from urllib.request import Request,urlopen

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/creature_editor'))
import server


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'docs/validation/equine-muzzle')
    parser.add_argument('--browser',default='google-chrome')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    temporary=Path('/tmp/opencode')
    with tempfile.TemporaryDirectory(dir=temporary if temporary.exists() else None,prefix='muzzle-editor-') as tmp:
        server.RECIPES=Path(tmp)/'recipes';server.RECIPES.mkdir()
        shutil.copy2(ROOT/'recipes/caballo.creature.json',server.RECIPES/'caballo.creature.json')
        http=server.ThreadingHTTPServer(('127.0.0.1',0),server.Handler);http.save_lock=threading.Lock()
        thread=threading.Thread(target=http.serve_forever,daemon=True);thread.start()
        base=f'http://127.0.0.1:{http.server_port}'
        def request(path,body=None):
            req=Request(base+path,data=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'})
            with urlopen(req,timeout=90) as response:return json.loads(response.read())
        try:
            request('/api/agent');schema=request('/api/agent/schema');library=request('/api/library')
            state=request('/api/agent/state')
            key=next(entry['key'] for entry in library if 'caballo.creature.json' in entry['key'])
            state=request('/api/agent/action',dict(action='open',key=key,expected_revision=state['revision']))
            state=request('/api/agent/action',dict(action='view',view=dict(stage='adult',part='head',camera='side',tab='paint'),expected_revision=state['revision']))
            (out/'editor-session.json').write_text(json.dumps(dict(source=state['source'],view=state['view'],revision=state['revision'],has_domain='anchorNode' in json.dumps(schema)),indent=2)+'\n')
            with (out/'editor-chrome.log').open('w') as log:
                subprocess.run([args.browser,'--headless','--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader','--hide-scrollbars',
                    '--user-data-dir='+str(Path(tmp)/'chrome'),'--window-size=1500,2200','--virtual-time-budget=20000',
                    '--screenshot='+str(out/'editor-controls.png'),base],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
            print('Editor capturado mediante API pública; catálogo aislado')
        finally:
            http.shutdown();http.server_close();thread.join()


if __name__=='__main__':
    main()
