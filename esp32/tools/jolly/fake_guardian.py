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
"""A stand-in Guardián for testing Jolly before the real one is up.

Serves the contract v1 fixtures (tests/jolly/contract/) as the Guardián would:
GET only, X-Guardian-Key checked, ETag and 304, JSON content type.

    tools/jolly/fake_guardian.py --scenario ok --port 3999 --host 0.0.0.0 --key KEY

Then on the board's console: >grd_url=http://<this Mac>:3999 and >grd_key=KEY.
Scenarios:
  ok     the healthy glance              crit   a backup alert and a down app
  stale  age_s 75: the device shows "Sin datos"
  401    every /v1 request is refused    down   connections are dropped, no answer
"""
import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

CONTRACT = Path(__file__).resolve().parents[2] / 'tests/jolly/contract'


def load(name):
    return json.loads((CONTRACT / f'{name}.json').read_text(encoding='utf-8'))


def bodies(scenario):
    glance = load('glance.crit' if scenario == 'crit' else 'glance.ok')
    if scenario == 'stale':
        glance['age_s'] = 75
    return {
        '/v1/glance': glance,
        '/v1/apps': load('apps'),
        '/v1/events': load('events'),
        ('/v1/series', '10m'): load('series.10m'),
        ('/v1/series', '24h'): load('series.24h'),
    }


def make_handler(scenario, key):
    data = bodies(scenario)

    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, fmt, *args):   # path and status only, never headers
            print(f'{self.command} {self.path.split("?")[0]} {args[1] if len(args) > 1 else ""}')

        def send_json(self, status, obj, etag=None):
            body = json.dumps(obj, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            if etag:
                self.send_header('ETag', etag)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def error(self, status, code):
            self.send_json(status, {'ok': False, 'error': code})

        def do_GET(self):
            if scenario == 'down':
                self.close_connection = True
                return   # no answer at all
            url = urlparse(self.path)
            if url.path == '/health':
                self.send_json(200, {'ok': True, 'v': 1, 'age_s': 3})
                return
            if not url.path.startswith('/v1/'):
                self.error(404, 'not_found')
                return
            if scenario == '401' or (key and self.headers.get('X-Guardian-Key') != key):
                self.error(401, 'bad_key')
                return
            if url.path == '/v1/series':
                q = parse_qs(url.query)
                obj = data.get(('/v1/series', q.get('w', [''])[0])) if q.get('m') == ['net'] else None
                prefix = 's'
            else:
                obj = data.get(url.path)
                prefix = url.path.split('/')[-1][0]
            if obj is None:
                self.error(400 if url.path == '/v1/series' else 404, 'bad_param' if url.path == '/v1/series' else 'not_found')
                return
            etag = f'"{prefix}{obj.get("seq", obj.get("ts", 0))}"'
            if self.headers.get('If-None-Match') == etag:
                self.send_response(304)
                self.send_header('ETag', etag)
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            self.send_json(200, obj, etag)

        def do_POST(self):
            self.error(405, 'method_not_allowed')

        do_PUT = do_DELETE = do_PATCH = do_POST

    return Handler


class Server(ThreadingHTTPServer):
    daemon_threads = True      # a board keeping its connection open doesn't hold up a stop
    block_on_close = False


def serve(scenario, host, port, key):
    return Server((host, port), make_handler(scenario, key))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--scenario', choices=['ok', 'crit', 'stale', '401', 'down'], default='ok')
    p.add_argument('--host', default='127.0.0.1', help='0.0.0.0 to reach it from the board')
    p.add_argument('--port', type=int, default=3999)
    p.add_argument('--key', default='', help='the key the board must send (empty: any)')
    a = p.parse_args()
    server = serve(a.scenario, a.host, a.port, a.key)
    print(f'fake Guardián ({a.scenario}) on http://{a.host}:{server.server_address[1]}')
    server.serve_forever()


if __name__ == '__main__':
    main()
