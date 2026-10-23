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

#include "muse_speech_split.h"

static bool space(char c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

/* Closing marks that belong to the sentence before them: " ' ) » ” at p.
 * Returns their length, 0 if none. */
static size_t closer(const char *t, size_t p, size_t len)
{
    unsigned char c = (unsigned char)t[p];
    if (c == '"' || c == '\'' || c == ')') {
        return 1;
    }
    if (p + 1 < len && c == 0xC2 && (unsigned char)t[p + 1] == 0xBB) {   /* » */
        return 2;
    }
    if (p + 2 < len && c == 0xE2 && (unsigned char)t[p + 1] == 0x80 && (unsigned char)t[p + 2] == 0x9D) {   /* ” */
        return 3;
    }
    return 0;
}

/* The sentence end's punctuation at p: its length, or 0. */
static size_t stop(const char *t, size_t p, size_t len)
{
    char c = t[p];
    if (c == '.' || c == '!' || c == '?') {
        return 1;
    }
    if (p + 2 < len && (unsigned char)c == 0xE2 &&
        (unsigned char)t[p + 1] == 0x80 && (unsigned char)t[p + 2] == 0xA6) {   /* … */
        return 3;
    }
    return 0;
}

size_t muse_speech_next(const char *text, size_t from, size_t len, bool final, size_t min_chars, size_t max_chars)
{
    if (from >= len) {
        return from;
    }
    size_t limit = len - from > max_chars ? from + max_chars : len;
    for (size_t i = from; i < limit; i++) {
        size_t end;
        if (text[i] == '\n') {
            end = i + 1;
        } else {
            size_t n = stop(text, i, len);
            if (!n) {
                continue;
            }
            size_t j = i + n, k;
            while (j < len && (k = closer(text, j, len))) {
                j += k;
            }
            if (j == len) {
                if (!final) {
                    continue;   /* can't tell yet: "3." may be "3.5" */
                }
                end = j;
            } else if (!space(text[j])) {
                continue;       /* "3.5", "a.m." */
            } else {
                end = j;
            }
        }
        while (end < len && space(text[end])) {
            end++;
        }
        if (end > limit) {
            break;
        }
        if (end - from >= min_chars || end == len) {
            return end;
        }
    }
    if (len - from <= max_chars) {
        return final ? len : from;
    }
    /* Too long without a sentence end: at the last space, else on a UTF-8 boundary. */
    for (size_t i = limit; i > from + 1; i--) {
        if (space(text[i - 1])) {
            return i;
        }
    }
    size_t end = limit;
    while (end > from + 1 && ((unsigned char)text[end] & 0xC0) == 0x80) {
        end--;
    }
    return end;
}
