# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Secret console commands: a malformed one is rejected, never forwarded or echoed."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MUSE = ROOT / 'components/muse'

HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "muse_secret.h"
int main(void) {
    /* Console-only families: never forwarded to the setup commands. */
    assert(muse_secret_console_only("tts_key=abc"));
    assert(muse_secret_console_only("tts_key"));
    assert(muse_secret_console_only("tts_keyX=abc"));
    assert(muse_secret_console_only("tts_url=http://x"));
    assert(muse_secret_console_only("sdk_token=mgst_abc"));
    assert(muse_secret_console_only("sdk_tokenmgst"));
    assert(muse_secret_console_only("grd_key=abc"));
    assert(!muse_secret_console_only("tts"));          /* the status command, no value */
    assert(!muse_secret_console_only("status"));
    assert(!muse_secret_console_only("hatch.token=abc"));
    assert(!muse_secret_console_only("volume=50"));
    assert(!muse_secret_console_only(""));
    /* Setup command names whose value is never echoed. */
    assert(muse_secret_value("wifi.pass"));
    assert(muse_secret_value("hatch.token"));
    assert(muse_secret_value("hatch.token+"));
    assert(muse_secret_value("tts_key"));
    assert(muse_secret_value("tts_keyX"));
    assert(muse_secret_value("sdk_token"));
    assert(muse_secret_value("grd_key"));
    assert(!muse_secret_value("wifi.ssid"));
    assert(!muse_secret_value("volume"));
    return 0;
}
'''


class SecretCommands(unittest.TestCase):
    def test_secret_command_classification(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            (out / 'harness.c').write_text(HARNESS)
            cc = shlex.split(os.environ.get('CC', 'cc'))
            build = subprocess.run([*cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(MUSE),
                                    str(out / 'harness.c'), str(MUSE / 'muse_secret.c'), '-o', str(out / 'h')],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(out / 'h')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
