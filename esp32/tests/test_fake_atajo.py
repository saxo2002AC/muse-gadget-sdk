# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0
"""tools/jolly/fake_atajo.py: contract v1.1's signed requests and answers, as
the board makes and checks them (jolly_fast_auth.c does the same maths)."""
import hashlib
import hmac
import http.client
import os
import sys
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/jolly'))
import fake_atajo  # noqa: E402

KEY = bytes(range(32))
PCM = b'\x01\x00' * 8000   # 0.5 s


class FakeAtajo(unittest.TestCase):
    def start(self, scenario):
        server = fake_atajo.serve(scenario, '127.0.0.1', 0, KEY)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        return server.server_address[1]

    def ask(self, port, body=PCM, ts=None, nonce=None, key=KEY, auth=None):
        ts = str(int(time.time()) if ts is None else ts)
        nonce = nonce or os.urandom(16).hex()
        conn = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
        headers = {
            'Content-Type': 'audio/L16; rate=16000; channels=1',
            'Content-Length': str(len(body)),
            'X-Jolly-Ts': ts,
            'X-Jolly-Nonce': nonce,
            'X-Jolly-Auth': auth or fake_atajo.sign_request(key, ts, nonce, body),
        }
        conn.request('POST', '/v1/ask', body=body, headers=headers)
        resp = conn.getresponse()
        data = resp.read()
        return resp.status, resp.getheader('X-Jolly-Sig'), data, nonce

    def test_signed_local_answer(self):
        port = self.start('local')
        status, sig, body, nonce = self.ask(port)
        self.assertEqual(status, 200)
        want = hmac.new(KEY, nonce.encode() + b'|' + body, hashlib.sha256).hexdigest()
        self.assertEqual(sig, want)
        self.assertIn(b'"action":"local"', body)

    def test_wrong_key_stale_ts_replay_and_bad_framing_are_refused(self):
        port = self.start('local')
        self.assertEqual(self.ask(port, key=bytes(32))[0], 401)                 # another key
        self.assertEqual(self.ask(port, ts=int(time.time()) - 61)[0], 401)      # outside ±60 s
        nonce = os.urandom(16).hex()
        self.assertEqual(self.ask(port, nonce=nonce)[0], 200)
        self.assertEqual(self.ask(port, nonce=nonce)[0], 401)                   # replayed
        self.assertEqual(self.ask(port, body=b'\x00' * 9599)[0], 400)           # odd, and too short
        self.assertEqual(self.ask(port, body=b'\x00' * 100)[0], 400)

    def test_bad_signature_scenario(self):
        port = self.start('badsig')
        status, sig, body, nonce = self.ask(port)
        self.assertEqual(status, 200)
        self.assertNotEqual(sig, hmac.new(KEY, nonce.encode() + b'|' + body, hashlib.sha256).hexdigest())

    def test_canonical_line_matches_the_firmware(self):
        # Same as tests/jolly/test_fast_auth.c's request_auth().
        sha = hashlib.sha256(b'\x01\x00\x02\x00').hexdigest()
        self.assertEqual(fake_atajo.canonical('1761200000', '00112233445566778899aabbccddeeff', sha),
                         f'POST|/v1/ask|1761200000|00112233445566778899aabbccddeeff|{sha}'.encode())


if __name__ == '__main__':
    unittest.main()
