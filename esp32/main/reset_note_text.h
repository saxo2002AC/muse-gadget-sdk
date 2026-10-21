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

#pragma once

#include <stddef.h>
#include <stdint.h>

// The text side of reset_note.c, pure so tests/test_reset_note.py checks it:
// the last few reasons setup credentials were wiped (or a step towards it),
// newest first, and the @whyreset line that prints them.

#define RESET_NOTE_MAX 64   // a reason, with its NUL
#define RESET_NOTE_KEEP 4   // how many are kept

typedef struct {
    char source[RESET_NOTE_MAX];
    uint32_t uptime_s;      // seconds since that boot
} reset_note_t;

// Printable ASCII only: anything else becomes '.', '"' becomes '\'' and '\\'
// becomes '/', so a reason can't break the JSON. NULL gives "".
void reset_note_clean(char *dst, size_t cap, const char *src);

// Puts a reason in front, dropping the oldest.
void reset_note_push(reset_note_t h[RESET_NOTE_KEEP], const char *source, uint32_t uptime_s);

// @whyreset {"count":N,"last":[{"source":..,"uptime_s":..},...]}, the empty
// entries left out.
void reset_note_json(char *out, size_t cap, const reset_note_t h[RESET_NOTE_KEEP], uint32_t count);
