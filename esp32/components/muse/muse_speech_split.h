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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Where the next piece of a reply to speak ends: in text[from, len), the
 * first sentence end (. ! ? … or a line break, with any closing quotes and
 * the spaces after) at least min_chars on, and at most max_chars on. A
 * sentence end needs what follows it unless the text is final ("3." may be
 * "3.5"). With no sentence end in max_chars, the cut is at the last space
 * before it, or at max_chars on a UTF-8 boundary. Final, what's left goes
 * too. Returns `from` when nothing is ready yet. Pure:
 * tests/jolly/test_speech_split.c.
 */
size_t muse_speech_next(const char *text, size_t from, size_t len, bool final, size_t min_chars, size_t max_chars);

#ifdef __cplusplus
}
#endif
