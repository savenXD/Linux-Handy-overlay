"""Real Chromium renderer/probe checks. Does not test Windows capture exclusion.
Requires Chrome and PowerShell; no login, target app, or existing browser profile.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--pwsh', default=shutil.which('pwsh'))
parser.add_argument('--chrome', default=shutil.which('google-chrome') or shutil.which('chrome'))
args, rest = parser.parse_known_args()

class ProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not args.pwsh or not args.chrome:
            raise unittest.SkipTest('Supply --pwsh and --chrome for the isolated renderer test')
        cls.work = tempfile.TemporaryDirectory(prefix='cronq-renderer-test-')
        cls.folder = Path(cls.work.name)
        fixture = cls.folder/'fixture.html'
        fixture.write_text("<title>Local renderer fixture</title><style>html,body{margin:0;background:rgb(21,122,211);color:white;font:30px sans-serif}h1{margin:30px}</style><h1>Renderer capture fixture</h1>")
        profile = cls.folder/'profile'
        cls.browser = subprocess.Popen([args.chrome,'--headless=new','--no-sandbox','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-port=0',f'--user-data-dir={profile}','--window-size=800,600',fixture.as_uri()],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                active = profile/'DevToolsActivePort'
                if active.exists():
                    cls.port = int(active.read_text().splitlines()[0])
                    try:
                        with urllib.request.urlopen(f'http://127.0.0.1:{cls.port}/json/list',timeout=1) as response:
                            pages=json.load(response)
                        matching=[p for p in pages if p.get('url')==fixture.as_uri()]
                        if matching:
                            cls.target=matching[0]['id']
                            break
                    except OSError: pass
                time.sleep(.05)
            else: raise RuntimeError('Isolated Chromium target did not start')
        except BaseException:
            cls.browser.terminate();cls.browser.wait(timeout=10);cls.work.cleanup();raise

    @classmethod
    def tearDownClass(cls):
        cls.browser.terminate()
        try: cls.browser.wait(timeout=10)
        except subprocess.TimeoutExpired: cls.browser.kill();cls.browser.wait()
        cls.work.cleanup()

    def probe(self,*options,port=None):
        command=[args.pwsh,'-NoProfile','-File',str(ROOT/'scripts/Capture-Renderer.ps1'),'-Port',str(port or self.port),*map(str,options)]
        result=subprocess.run(command,capture_output=True,text=True,timeout=20)
        self.assertTrue(result.stdout.strip(),result.stderr)
        return result.returncode,json.loads(result.stdout)

    def test_list_requires_explicit_selection_and_excludes_page_details(self):
        code,result=self.probe('-ListTargets')
        self.assertEqual(code,0,result)
        self.assertIn(self.target,[t['targetId'] for t in result['targets']])
        self.assertTrue(all(set(t)=={'targetId','type'} for t in result['targets']))

    def test_real_renderer_png_and_fresh_output(self):
        output=self.folder/'captures'
        previous=None
        for _ in range(2):
            code,result=self.probe('-TargetId',self.target,'-OutputDirectory',output)
            self.assertEqual(code,0,result)
            self.assertEqual(result['status'],'CapturedUnverified')
            self.assertFalse(result['contentVerified'])
            path=Path(result['path'])
            self.assertNotEqual(path,previous)
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),result['sha256'])
            with Image.open(path) as image:
                self.assertEqual(image.size,(result['width'],result['height']))
                self.assertEqual(image.convert('RGB').getpixel((100,image.height-60)),(21,122,211))
            previous=path
        self.assertEqual(len(list(output.glob('*.png'))),2)

    def test_wrong_target_never_captures_another_page(self):
        output=self.folder/'wrong-target'
        code,result=self.probe('-TargetId','absent-target','-OutputDirectory',output)
        self.assertEqual(code,2)
        self.assertEqual(result['stage'],'selection')
        self.assertFalse(output.exists())

    def test_untrusted_discovery_cannot_redirect_websocket(self):
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                data=json.dumps([dict(id='test-target',type='page',webSocketDebuggerUrl='ws://192.0.2.1:9222/devtools/page/test-target')]).encode()
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
            def log_message(self,*_): pass
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            code,result=self.probe('-TargetId','test-target',port=server.server_port)
            self.assertEqual(code,2)
            self.assertEqual(result['stage'],'selection')
            self.assertIn('selected local port',result['message'])
        finally: server.shutdown();server.server_close();thread.join()

    def test_unavailable_endpoint_reports_failure_without_capture(self):
        server=ThreadingHTTPServer(('127.0.0.1',0),BaseHTTPRequestHandler)
        port=server.server_port;server.server_close()
        code,result=self.probe('-ListTargets',port=port)
        self.assertEqual(code,2)
        self.assertEqual(result['stage'],'discovery')
        self.assertIn('cannot enable',result['message'])

if __name__=='__main__': unittest.main(argv=[__file__,*rest],verbosity=2)
