/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * reset_note_text: the reasons a setup reset (or a step towards one) is kept
 * for, and printed by, >whyreset.
 */
#include <assert.h>
#include <string.h>

#include "reset_note_text.h"

static void clean(void)
{
    char out[RESET_NOTE_MAX];
    reset_note_clean(out, sizeof(out), "server unpair (node.unpaired)");
    assert(!strcmp(out, "server unpair (node.unpaired)"));
    reset_note_clean(out, sizeof(out), "a\"b\\c\nd\x01" "e\xc3\xa9");
    assert(!strcmp(out, "a'b/c.d.e.."));   /* nothing that breaks the @whyreset JSON */
    char longer[200];
    memset(longer, 'x', sizeof(longer) - 1);
    longer[sizeof(longer) - 1] = '\0';
    reset_note_clean(out, sizeof(out), longer);
    assert(strlen(out) == RESET_NOTE_MAX - 1);
    reset_note_clean(out, sizeof(out), NULL);
    assert(!strcmp(out, ""));
}

static void history(void)
{
    reset_note_t h[RESET_NOTE_KEEP];
    memset(h, 0, sizeof(h));
    reset_note_push(h, "first", 10);
    reset_note_push(h, "second", 20);
    assert(!strcmp(h[0].source, "second") && h[0].uptime_s == 20);   /* newest first */
    assert(!strcmp(h[1].source, "first") && h[1].uptime_s == 10);
    for (int i = 0; i < 5; i++) {
        reset_note_push(h, "more", 30 + i);
    }
    assert(h[0].uptime_s == 34 && h[RESET_NOTE_KEEP - 1].uptime_s == 30 + 5 - RESET_NOTE_KEEP);

    char json[512];
    memset(h, 0, sizeof(h));
    reset_note_json(json, sizeof(json), h, 0);
    assert(!strcmp(json, "@whyreset {\"count\":0,\"last\":[]}"));
    reset_note_push(h, "deferred setup reset", 512);
    reset_note_push(h, "pairing cleared: token rejected", 600);
    reset_note_json(json, sizeof(json), h, 7);
    assert(!strcmp(json, "@whyreset {\"count\":7,\"last\":[{\"source\":\"pairing cleared: token rejected\","
                         "\"uptime_s\":600},{\"source\":\"deferred setup reset\",\"uptime_s\":512}]}"));
}

int main(void)
{
    clean();
    history();
    return 0;
}
