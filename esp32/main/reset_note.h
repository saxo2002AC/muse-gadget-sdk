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

#include <stdbool.h>

// Why setup credentials went: every path that wipes them (or forgets Wi-Fi,
// which makes the next boot wipe them) calls reset_note() with its source.
// It logs at WARN and keeps the last few with their uptime in NVS, in a
// namespace of its own that no reset clears. >whyreset prints them.

void reset_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// The serial console's >whyreset; false for any other line.
bool reset_note_console_command(const char *line);
