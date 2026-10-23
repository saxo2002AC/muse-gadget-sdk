# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Exercise production PSRAM reply handlers with host-side event sinks."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JSON = Path(os.environ.get(
    'CJSON_SOURCE_DIR', ROOT / 'managed_components/espressif__cjson/cJSON'
))


class ChatSession(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        out = Path(cls.tmp.name)
        source = (ROOT / 'components/muse/muse_chat_session.cpp').read_text()
        constants = source[source.index('#define MIC_RATE'):source.index('/* ---- Voice task')]
        types = source[source.index('enum phase_t'):source.index('/* 10 KB')]
        handlers = source[source.index('static int find_msg('):source.index('static void on_chat_ack(')]
        reset = source[source.index('static void turn_clear('):source.index('static void turn_begin(')]
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <initializer_list>
#include "host_compat.h"
#include "muse_speech_split.h"
#include "cJSON.h"
#include "minimp3.h"
#include "muse_chat_priv.h"
#define CONFIG_MUSE_ENABLED 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(tag, ...) ((void)(tag))
static const char *TAG = "test";
static bool tts_set = false, speaker_on = true;
static bool muse_tts_configured() { return tts_set; }
static bool muse_settings_speaker_on() { return speaker_on; }
''' + constants + types + r'''
static turn_t s_turn;
static std::atomic<uint32_t> s_gen{0};
static std::atomic<bool> s_push{false};
static int64_t s_sub_us = -10000000;   /* subscribed long ago: what arrives now is news */
static char s_reply_shown[EV_TEXT];
static int64_t s_last_seq, s_marks[4];
static int captions, console_events;
enum mark_t { M_TEXT, M_DONE };
static void mark(mark_t) {}
static int64_t now_us() { return 12345; }
static void emit(muse_hatch_ev_t type, const char *) {
    if (type == MUSE_HATCH_EV_REPLY) captions++;
}
void muse_hatch_console(const char *, const char *, const char *, ...) { console_events++; }
void muse_hatch_tail_words(const char *text, char *out, size_t cap) { strlcpy(out, text, cap); }
bool muse_hatch_caption_at(const char *text, size_t, char *out, size_t cap) {
    strlcpy(out, text, cap); return text[0];
}
static void turn_finish() { s_turn.phase = P_IDLE; }
static void turn_fail(const char *) { turn_finish(); }
static bool ensure_connected() { return true; }
bool muse_hatch_configured() { return true; }
static void resampler_init(resampler_t *, int, int) {}
''' + reset + handlers + r'''
static void begin(bool typed = false) {
    assert(turn_start(s_turn.gen + 1, typed));
    s_turn.phase = P_WAIT_REPLY;
    s_turn.acked = true;
    strlcpy(s_turn.user_ids[0], "note", sizeof(s_turn.user_ids[0]));
    strlcpy(s_turn.user_ids[1], "parent", sizeof(s_turn.user_ids[1]));
    captions = console_events = 0;
}
static void event(const char *kind, const char *id, const char *parent = "", const char *text = "") {
    cJSON *root = cJSON_CreateObject(), *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "event");
    cJSON_AddStringToObject(root, "event", kind);
    cJSON_AddItemToObject(root, "payload", payload);
    cJSON_AddStringToObject(payload, "message_id", id);
    if (parent[0]) cJSON_AddStringToObject(payload, "reply_to_message_id", parent);
    cJSON_AddStringToObject(payload, "text", text);
    cJSON_AddStringToObject(payload, "display_text", text);
    on_event(root);
    cJSON_Delete(root);
}
static void rejected_deltas() {
    for (bool typed : {false, true}) {
        begin(typed);
        event("delta.message_start", "other", "elsewhere");
        event("delta.text_append", "other", "", "Wrong reply");
        event("delta.message_done", "other");
        event("message.assistant", "other", "", "Wrong final");
        assert(!s_turn.nmsgs && !captions && !console_events);
        assert(!s_turn.last_content_us && !s_turn.last_event_us);
        event("delta.message_start", "reply", "note");
        event("delta.text_append", "reply", "", "Our reply");
        event("delta.message_done", "reply");
        assert(s_turn.nmsgs == 1 && s_turn.msgs[0].done && s_turn.msgs[0].len == 9);
        assert(typed ? console_events == 2 : captions == 1);
        begin(typed);
        event("message.assistant", "other", "", "New turn");
        assert(s_turn.nmsgs == 1 && s_turn.msgs[0].done);
    }
}
static void valid_parents() {
    begin();
    event("message.assistant", "first", "note", "First");
    event("message.assistant", "second", "parent", "Second");
    event("message.assistant", "third", "first", "Third");
    event("message.assistant", "fourth", "", "Live");
    assert(s_turn.nmsgs == 4);
    for (int i = 0; i < s_turn.nmsgs; i++) assert(s_turn.msgs[i].done);
    cJSON *payload = cJSON_Parse("{\"parent_message_id\":\"elsewhere\"}");
    assert(bind_msg("fallback", payload) == -1);
    cJSON_Delete(payload);
    event("message.assistant", "fallback", "", "Wrong final");
    assert(s_turn.nmsgs == 4);
}
static void bounded_rejections() {
    begin();
    event("delta.message_start", "reply", "note");
    char id[20];
    for (int i = 0; i < 9; i++) { /* exceed the eight rejected IDs retained per turn */
        snprintf(id, sizeof(id), "other%d", i);
        event("delta.message_start", id, "elsewhere");
    }
    event("message.assistant", "other0", "", "Wrong final");
    event("message.assistant", id, "", "Overflow final");
    assert(s_turn.nmsgs == 1 && !captions && !console_events);
    event("delta.text_append", "reply", "", "Our reply");
    event("delta.message_done", "reply");
    event("message.assistant", "second", "note", "Second");
    assert(s_turn.nmsgs == 2 && s_turn.msgs[0].done && s_turn.msgs[1].done);
}
static int speaking() {
    int n = 0;
    for (int i = 0; i < s_turn.nmsgs; i++) n += s_turn.msgs[i].speak;
    return n;
}
static void speech_queue() {
    char texts[MAX_MSGS * TEXT_MAX] = {};
    s_turn.texts = texts;
    tts_set = false;   /* no TTS server: every message is text */
    begin();
    event("message.assistant", "a", "note", "First");
    assert(s_turn.nmsgs == 1 && s_turn.msgs[0].tts == TTS_QUEUED && !speaking());
    tts_set = true;   /* in order, up to SPEAK_QUEUE_MAX; past that the oldest waiting goes as text */
    begin();
    const char *ids[] = { "m0", "m1", "m2", "m3", "m4" };
    for (const char *id : ids) event("message.assistant", id, "note", "Some text.");
    assert(s_turn.nmsgs == 5 && speaking() == SPEAK_QUEUE_MAX);
    assert(!s_turn.msgs[0].speak && s_turn.msgs[1].speak && s_turn.msgs[4].speak);
    for (int i = 0; i < 5; i++) assert(s_turn.msgs[i].tts == TTS_QUEUED);
    speaker_on = false;   /* speaker off: nothing queued to speak */
    begin();
    event("message.assistant", "q", "note", "Quiet");
    assert(s_turn.nmsgs == 1 && !speaking());
    speaker_on = true;
    tts_set = false;
}
/* Muse's story after the turn ended: a reply of its own, spoken from its
 * first whole sentence while the rest arrives, and queued once. */
static void late_message() {
    char texts[MAX_MSGS * TEXT_MAX] = {};
    s_turn.texts = texts;
    tts_set = true;
    begin();
    event("message.assistant", "a", "note", "Perfecto, si te la creo.");
    turn_finish();
    s_push = false;
    assert(s_turn.phase == P_IDLE);
    event("delta.message_start", "story", "someone-else");
    assert(s_push && s_turn.push && s_turn.phase == P_WAIT_REPLY);
    assert(s_turn.nmsgs == 1 && !strcmp(s_turn.msgs[0].id, "story"));
    event("delta.text_append", "story", "", "Hab");
    assert(s_turn.msgs[0].tts == TTS_NONE);                     /* no whole sentence yet */
    event("delta.text_append", "story", "", "ia una vez una nina muy curiosa. Un dia");
    assert(s_turn.msgs[0].tts == TTS_QUEUED && speaking() == 1);   /* starts on the first sentence */
    event("delta.text_append", "story", "", " fue al bosque.");
    event("delta.message_done", "story");
    assert(s_turn.msgs[0].done && speaking() == 1);              /* not queued twice */
    /* Right after subscribing, what arrives is the backlog: not played. */
    turn_finish();
    s_push = false;
    int64_t was = s_sub_us;
    s_sub_us = now_us();
    event("message.assistant", "old", "", "Old news.");
    assert(!s_push && s_turn.phase == P_IDLE);
    s_sub_us = was;
    /* Speaker off: a late message is still taken, shown as text once done. */
    speaker_on = false;
    event("message.assistant", "late", "", "Shown.");
    assert(s_push && s_turn.nmsgs == 1 && s_turn.msgs[0].done && !speaking());
    speaker_on = true;
    tts_set = false;
    turn_finish();
    s_push = false;
}
/* The atajo answered locally (parallel mode): Muse's reply to that turn,
 * in it or late, is never played. */
static void dropped_turn() {
    char texts[MAX_MSGS * TEXT_MAX] = {};
    s_turn.texts = texts;
    begin();                                       /* acked: user ids "note" and "parent" */
    suppress_turn_replies(now_us());
    turn_finish();
    s_push = false;
    event("message.assistant", "late", "note", "Muse's own answer.");
    assert(!s_push && s_turn.phase == P_IDLE);     /* a late reply to it: not played */
    event("delta.message_start", "x", "", "");     /* nor anything without a parent, for a while */
    assert(!s_push && s_turn.phase == P_IDLE);
    /* A new turn: a reply to the dropped note is still refused. */
    begin();
    strlcpy(s_turn.user_ids[0], "new", sizeof(s_turn.user_ids[0]));
    s_turn.user_ids[1][0] = '\0';
    event("message.assistant", "old", "note", "Muse's own answer.");
    assert(s_turn.nmsgs == 0);
    event("message.assistant", "mine", "new", "The new answer.");
    assert(s_turn.nmsgs == 1);
    /* Once the window is over, late messages play again. */
    turn_finish();
    s_push = false;
    s_drop_until_us = now_us() - 1;
    event("message.assistant", "later", "", "Later news.");
    assert(s_push);
    turn_finish();
    s_push = false;
    memset(s_drop_ids, 0, sizeof(s_drop_ids));
}
/* The atajo beat Muse's ack: the dropped note's ids never came. Muse's
 * reply to it still mustn't play, not in a quick new turn either. */
static void dropped_before_ack() {
    char texts[MAX_MSGS * TEXT_MAX] = {};
    s_turn.texts = texts;
    memset(s_drop_msgs, 0, sizeof(s_drop_msgs));
    begin();
    s_turn.acked = false;
    s_turn.user_ids[0][0] = s_turn.user_ids[1][0] = '\0';
    suppress_turn_replies(now_us());
    turn_finish();
    s_push = false;
    event("delta.message_start", "r1", "dropped-note", "");          /* idle: not played, remembered */
    assert(!s_push && s_turn.phase == P_IDLE);
    /* A new turn, before its own ack: nothing binds. */
    begin();
    s_turn.acked = false;
    s_turn.user_ids[0][0] = s_turn.user_ids[1][0] = '\0';
    event("delta.text_append", "r1", "", "Muse's answer to the old one");
    event("message.assistant", "r2", "dropped-note", "Its other message");
    assert(s_turn.nmsgs == 0);
    /* Its ack: only replies to it bind; parentless ones don't, in the window. */
    s_turn.acked = true;
    strlcpy(s_turn.user_ids[0], "new", sizeof(s_turn.user_ids[0]));
    event("message.assistant", "r3", "", "Parentless");
    assert(s_turn.nmsgs == 0);
    event("message.assistant", "mine", "new", "The new answer.");
    assert(s_turn.nmsgs == 1);
    /* Past the window, the old reply's later deltas are still refused by id. */
    turn_finish();
    s_push = false;
    s_drop_until_us = now_us() - 1;
    event("delta.text_append", "r1", "", "more of the old answer");
    assert(!s_push);
    begin();
    event("delta.text_append", "r1", "", "more of the old answer");
    assert(s_turn.nmsgs == 0);
    turn_finish();
    s_push = false;
    memset(s_drop_ids, 0, sizeof(s_drop_ids));
    memset(s_drop_msgs, 0, sizeof(s_drop_msgs));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    switch (atoi(argv[1])) {
    case 0: rejected_deltas(); break;
    case 1: valid_parents(); break;
    case 2: bounded_rejections(); break;
    case 3: speech_queue(); break;
    case 4: late_message(); break;
    case 5: dropped_turn(); break;
    case 6: dropped_before_ack(); break;
    default: return 2;
    }
}
'''
        (out / 'session.cpp').write_text(code)
        flags = ['-Wall', '-Wextra', '-Werror', '-I', str(JSON),
                 '-I', str(ROOT / 'tests'), '-I', str(ROOT / 'components/muse'),
                 '-I', str(ROOT / 'components/minimp3/include')]
        commands = [
            [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', *flags,
             '-c', str(JSON / 'cJSON.c'), '-o', str(out / 'cjson.o')],
            [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', *flags,
             '-c', str(ROOT / 'components/muse/muse_speech_split.c'), '-o', str(out / 'split.o')],
            [*shlex.split(os.environ.get('CXX', 'c++')), '-std=gnu++17', *flags,
             str(out / 'session.cpp'), str(out / 'cjson.o'), str(out / 'split.o'), '-o', str(out / 'session')],
        ]
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
        cls.binary = out / 'session'

    def run_case(self, case):
        result = subprocess.run([str(self.binary), str(case)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejected_deltas_do_not_emit_or_complete_voice_or_typed_replies(self):
        self.run_case(0)

    def test_ack_parent_reply_chain_and_parentless_messages_still_work(self):
        self.run_case(1)

    def test_rejection_capacity_preserves_correlation(self):
        self.run_case(2)

    def test_finished_messages_queue_for_speech_in_order_and_bounded(self):
        self.run_case(3)

    def test_late_message_after_the_turn_is_played_and_spoken_early(self):
        self.run_case(4)

    def test_a_turn_answered_locally_never_plays_muses_reply(self):
        self.run_case(5)

    def test_a_drop_before_the_ack_still_never_plays_muses_reply(self):
        self.run_case(6)
