#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""A stand-in jolly-atajo for testing the board's atajo before the real one is up.

Speaks contract v1.1 (tests/jolly/contract/atajo/README.md): POST /v1/ask
with signed requests (X-Jolly-Ts, X-Jolly-Nonce, X-Jolly-Auth; the headers
checked before the body is read) and signed answers (X-Jolly-Sig =
HMAC(K, "RESP|nonce|status|sha256hex(body)")), as Fernando's contract.

    tools/jolly/fake_atajo.py --scenario local --port 3998 --host 0.0.0.0 --key-file ~/.jolly_atajo_key

The key comes from a file (64 hex digits), never from argv (Diana C8). Then
on the board's console: >fast_url=http://<this Mac>:3998, >fast_key=<key>,
>fast=1. Scenarios:
  local    ask.local.hora          forward  ask.forward.no_match
  slow     answers after 2.5 s     500      an internal error
  garbage  200 with a non-JSON body big  200 with a 1025-byte body
  badsig   200 local with a wrong X-Jolly-Sig
  refuse   closes the connection without answering
"""
import argparse
import hashlib
import hmac
import json
import re
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

CONTRACT = Path(__file__).resolve().parents[2] / 'tests/jolly/contract/atajo'
TS_WINDOW_S = 60
NONCE_TTL_S = 120
LEN_MIN, LEN_MAX = 9600, 480000


def load(name):
    return (CONTRACT / f'{name}.json').read_bytes()


def canonical(ts, nonce, body_sha_hex):
    return f'POST|/v1/ask|{ts}|{nonce}|{body_sha_hex}'.encode()


def sign_request(key, ts, nonce, body):
    return hmac.new(key, canonical(ts, nonce, hashlib.sha256(body).hexdigest()), hashlib.sha256).hexdigest()


def sign_answer(key, nonce, status, body):
    msg = f'RESP|{nonce}|{status}|{hashlib.sha256(body).hexdigest()}'.encode()
    return hmac.new(key, msg, hashlib.sha256).hexdigest()


def make_handler(scenario, key):
    seen = {}   # nonce -> when

    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, fmt, *args):   # path and status only, never headers or bodies
            pass

        def reply(self, status, body, ctype='application/json; charset=utf-8', sig=None):
            self.send_response(status)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Connection', 'close')
            if sig:
                self.send_header('X-Jolly-Sig', sig)
            self.end_headers()
            self.wfile.write(body)
            self.close_connection = True

        def error(self, status, code):
            self.reply(status, json.dumps({'ok': False, 'error': code}).encode())

        def do_POST(self):
            if self.path != '/v1/ask':
                return self.error(404, 'not_found')
            ts, nonce, auth = (self.headers.get(h, '') for h in ('X-Jolly-Ts', 'X-Jolly-Nonce', 'X-Jolly-Auth'))
            length = self.headers.get('Content-Length', '')
            ctype = self.headers.get('Content-Type', '').replace(' ', '').lower()
            # Everything that needs no body first (Diana C1): nothing is read from a refused peer.
            if not re.fullmatch(r'[0-9]{1,12}', ts) or not re.fullmatch(r'[0-9a-f]{32}', nonce) \
                    or not re.fullmatch(r'[0-9a-f]{64}', auth):
                return self.error(401, 'bad_auth')
            if abs(time.time() - int(ts)) > TS_WINDOW_S:
                return self.error(401, 'bad_auth')
            now = time.time()
            for n, t in list(seen.items()):
                if now - t > NONCE_TTL_S:
                    del seen[n]
            if nonce in seen:
                return self.error(401, 'replay')
            if 'transfer-encoding' in self.headers or not re.fullmatch(r'[0-9]{1,6}', length) \
                    or not ctype.startswith('audio/l16') or 'rate=16000' not in ctype:
                return self.error(400, 'bad_request')
            n = int(length)
            if n % 2 or n < LEN_MIN:
                return self.error(400, 'bad_request')
            if n > LEN_MAX:
                return self.error(413, 'too_large')
            body = self.rfile.read(n)
            if not hmac.compare_digest(auth, sign_request(key, ts, nonce, body)):
                return self.error(401, 'bad_auth')
            seen[nonce] = now
            if scenario == 'refuse':
                self.close_connection = True
                self.connection.close()
                return
            if scenario == 'slow':
                time.sleep(2.5)
            if scenario == '500':
                return self.error(500, 'internal')
            if scenario == 'garbage':
                return self.reply(200, b'not json at all', sig=sign_answer(key, nonce, 200, b'not json at all'))
            if scenario == 'big':
                big = load('ask.local.hora').rstrip(b'}') + b',"pad":"' + b'x' * 1100 + b'"}'
                return self.reply(200, big[:1025], sig=sign_answer(key, nonce, 200, big[:1025]))
            answer = load('ask.forward.no_match' if scenario == 'forward' else 'ask.local.hora')
            sig = sign_answer(key, nonce, 200, answer)
            if scenario == 'badsig':
                sig = sign_answer(key, '0' * 32, 200, answer)
            self.reply(200, answer, sig=sig)

    return Handler


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def serve(scenario, host, port, key):
    return Server((host, port), make_handler(scenario, key))


def read_key(path):
    text = Path(path).expanduser().read_text().strip()
    if not re.fullmatch(r'[0-9a-fA-F]{64}', text):
        raise SystemExit('the key file must hold 64 hex digits')
    return bytes.fromhex(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--scenario', default='local',
                    choices=['local', 'forward', 'slow', '500', 'garbage', 'big', 'badsig', 'refuse'])
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=3998)
    ap.add_argument('--key-file', required=True)
    args = ap.parse_args()
    server = serve(args.scenario, args.host, args.port, read_key(args.key_file))
    print(f'fake jolly-atajo ({args.scenario}) on http://{args.host}:{args.port}/v1/ask', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
