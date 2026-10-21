# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0
"""tools/jolly/fake_guardian.py answers as the Guardián's contract v1 says."""
import importlib.util
import json
from pathlib import Path
import threading
import unittest
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fake_guardian', ROOT / 'tools/jolly/fake_guardian.py')
fake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fake)


class FakeGuardian(unittest.TestCase):
    def start(self, scenario, key='k'):
        server = fake.serve(scenario, '127.0.0.1', 0, key)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        return f'http://127.0.0.1:{server.server_address[1]}'

    def get(self, url, headers=None):
        req = urllib.request.Request(url, headers=headers or {})
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()

    def test_glance_key_etag(self):
        base = self.start('ok')
        status, _, _ = self.get(base + '/v1/glance')
        self.assertEqual(status, 401)
        status, headers, body = self.get(base + '/v1/glance', {'X-Guardian-Key': 'k'})
        self.assertEqual(status, 200)
        self.assertTrue(headers['Content-Type'].startswith('application/json'))
        self.assertLessEqual(len(body), 1024)
        self.assertEqual(json.loads(body)['v'], 1)
        status, _, body = self.get(base + '/v1/glance', {'X-Guardian-Key': 'k', 'If-None-Match': headers['ETag']})
        self.assertEqual((status, body), (304, b''))

    def test_scenarios(self):
        base = self.start('stale')
        _, _, body = self.get(base + '/v1/glance', {'X-Guardian-Key': 'k'})
        self.assertEqual(json.loads(body)['age_s'], 75)
        base = self.start('crit')
        _, _, body = self.get(base + '/v1/glance', {'X-Guardian-Key': 'k'})
        self.assertEqual(json.loads(body)['level'], 'crit')
        base = self.start('401')
        self.assertEqual(self.get(base + '/v1/glance', {'X-Guardian-Key': 'k'})[0], 401)

    def test_series_and_bad_params(self):
        base = self.start('ok')
        h = {'X-Guardian-Key': 'k'}
        _, _, body = self.get(base + '/v1/series?m=net&w=24h', h)
        self.assertEqual(len(json.loads(body)['rx']), 24)
        self.assertEqual(self.get(base + '/v1/series?m=cpu&w=24h', h)[0], 400)
        self.assertEqual(self.get(base + '/v1/nope', h)[0], 404)


if __name__ == '__main__':
    unittest.main()
