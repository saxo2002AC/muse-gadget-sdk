// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "reset_note_text.h"

#include <stdio.h>
#include <string.h>

void reset_note_clean(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t n = 0;
    for (; src && src[n] && n + 1 < cap; n++) {
        unsigned char c = (unsigned char)src[n];
        dst[n] = c == '"' ? '\'' : c == '\\' ? '/' : (c < 0x20 || c > 0x7e) ? '.' : (char)c;
    }
    dst[n] = '\0';
}

void reset_note_push(reset_note_t h[RESET_NOTE_KEEP], const char *source, uint32_t uptime_s) {
    memmove(&h[1], &h[0], (RESET_NOTE_KEEP - 1) * sizeof(h[0]));
    reset_note_clean(h[0].source, sizeof(h[0].source), source);
    h[0].uptime_s = uptime_s;
}

void reset_note_json(char *out, size_t cap, const reset_note_t h[RESET_NOTE_KEEP], uint32_t count) {
    size_t at = 0;
    int n = snprintf(out, cap, "@whyreset {\"count\":%u,\"last\":[", (unsigned)count);
    at = n > 0 && (size_t)n < cap ? (size_t)n : cap;
    int first = 1;
    for (int i = 0; i < RESET_NOTE_KEEP && at < cap; i++) {
        if (!h[i].source[0]) continue;
        char clean[RESET_NOTE_MAX];
        reset_note_clean(clean, sizeof(clean), h[i].source);
        n = snprintf(out + at, cap - at, "%s{\"source\":\"%s\",\"uptime_s\":%u}", first ? "" : ",", clean,
                     (unsigned)h[i].uptime_s);
        at += n > 0 && (size_t)n < cap - at ? (size_t)n : cap - at;
        first = 0;
    }
    if (at < cap) snprintf(out + at, cap - at, "]}");
}
