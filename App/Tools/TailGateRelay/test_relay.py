import http.client
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('relay', HERE / 'relay.py')
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)


class RelayTests(unittest.TestCase):
    def setUp(self):
        self.server = relay.HTTPServer(('127.0.0.1', 0), relay.Handler)
        self.server.config = {'enabled': False, 'token': '', 'chat': ''}
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def request(self, path):
        conn = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        conn.request('GET', path)
        response = conn.getresponse()
        status = response.status
        response.read()
        conn.close()
        return status

    def test_default_off_and_validation(self):
        with patch('urllib.request.urlopen', side_effect=AssertionError('network forbidden')):
            self.assertEqual(self.request('/?msg=hello+world'), 200)
            for path in ('/', '/other?msg=x', '/?msg=', '/?msg=a&msg=b', '/?msg=' + 'a' * 513):
                self.assertEqual(self.request(path), 400)

    def test_forward_payload_and_failure(self):
        self.server.config = {'enabled': True, 'token': 'test-token', 'chat': '123'}
        with patch('urllib.request.urlopen') as send:
            send.return_value.__enter__.return_value.status = 200
            self.assertEqual(self.request('/?msg=hello+world'), 200)
            req = send.call_args.args[0]
            self.assertEqual(req.data, b'chat_id=123&text=hello+world')
        with patch('urllib.request.urlopen', side_effect=OSError('secret')):
            self.assertEqual(self.request('/?msg=x'), 502)

    def test_safe_defaults(self):
        with patch.dict(os.environ, {}, clear=True):
            c = relay.configuration()
            self.assertEqual(c['host'], '127.0.0.1')
            self.assertFalse(c['enabled'])


class ThermalTests(unittest.TestCase):
    def test_actual_blob_algorithm(self):
        compiler = shutil.which('g++')
        if not compiler:
            self.skipTest('g++ not available')
        root = HERE.parents[2]
        source = (root / 'SurveyFirmware/TailGateDetector/TailGateDetector.ino').read_text()
        algorithm = source[source.index('int countPeople('):source.index('// ---------------- entry result')]
        harness = '''#include <cassert>\nconst float PERSON_MARGIN=2.5; const int MIN_BLOB=2; float delta[64]={};\n''' + algorithm + '''\nint main(){int big=-1; assert(countPeople(&big)==0 && big==0); delta[0]=3; assert(countPeople(&big)==0); delta[1]=3; delta[16]=3; delta[17]=3; assert(countPeople(&big)==2 && big==2); delta[8]=3; assert(countPeople(&big)==1 && big==5); for(int i=0;i<64;i++)delta[i]=3; assert(countPeople(&big)==1 && big==64); }'''
        with tempfile.TemporaryDirectory() as d:
            cpp = Path(d) / 'test.cpp'
            exe = Path(d) / 'test.exe'
            cpp.write_text(harness)
            subprocess.run([compiler, '-std=c++11', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
