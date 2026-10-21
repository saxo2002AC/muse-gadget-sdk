/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Spoken replies from a TTS server of your own on the local network.
 *
 * Muse's replies are text. With a server configured, each reply message's
 * text goes to it and the speech it streams back plays through the reply
 * audio path, captions following (start_tts in muse_chat_session.cpp). Without
 * one, or on any failure, replies stay text, as before.
 *
 * The server's contract:
 *   POST <url>/tts, header X-Muse-Voz-Key: <key>,
 *   Content-Type: text/plain; charset=utf-8, body the text (up to 4000 bytes)
 *   200: audio/L16; rate=16000; channels=1 (raw PCM s16le mono, streamed)
 *   401/403/413/500: a JSON error
 *
 * The URL and key live in NVS, set on the serial console only (never over
 * BLE): ">tts_url=http://Mac-mini-de-acserver.local:3024" (or an IPv4 address;
 * http://host:port only, muse_url.h; a .local name is found by mDNS and asked
 * again after a failed connect, muse_resolve.h) answers "@tts_url ok", and
 * ">tts_key=..." answers "@tts_key ok". An empty value clears either, and
 * ">tts" prints "@tts {...}" with the URL and the key's length. The key is
 * never logged or printed. CONFIG_MUSE_TTS_URL and CONFIG_MUSE_TTS_KEY are
 * the defaults when NVS holds none.
 */

/* Loads the saved URL and key. Called from muse_hatch_start(); safe to repeat. */
void muse_tts_init(void);

/* True if a URL is set (with or without a key). */
bool muse_tts_configured(void);

/* Handles a console line without its '>'; false if it isn't one of these. */
bool muse_tts_console_command(const char *line);

/*
 * Starts speaking `len` bytes of `text` (UTF-8). `cut` says the text was
 * truncated by the caller, so it ends at the last sentence that's whole.
 * False if no server is set or the previous request is still winding down:
 * the caller shows the message as text.
 */
bool muse_tts_begin(const char *text, size_t len, bool cut);

typedef enum {
    MUSE_TTS_IDLE,      /* nothing started */
    MUSE_TTS_RUNNING,   /* the server may send more */
    MUSE_TTS_ENDED,     /* the whole response arrived */
    MUSE_TTS_FAILED,    /* no answer, an error status, or the stream broke */
} muse_tts_state_t;

/* True while a request is open, or a cancelled one is still closing. */
bool muse_tts_busy(void);

/* The request's state; *got_audio says whether any speech arrived. */
muse_tts_state_t muse_tts_state(bool *got_audio);

/* Up to `frames` 16 kHz mono samples of speech, without waiting; 0 if none yet. */
size_t muse_tts_read(int16_t *pcm, size_t frames);

/* Samples waiting to be read. */
size_t muse_tts_pending(void);

/* Stops the request: the stream is closed and what's buffered is dropped. */
void muse_tts_cancel(void);

#ifdef __cplusplus
}
#endif
