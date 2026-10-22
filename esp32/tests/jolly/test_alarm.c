/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_alarm: a new crit alarms for a while, then calms; a standing one
 * only reminds, once an hour; one already open at boot alarms 30 s at most.
 */
#include <assert.h>

#include "jolly/jolly_alarm.h"

#define S 1000LL
#define MIN (60 * S)
#define HOUR (60 * MIN)

static jolly_alarm_obs_t obs(jolly_level_t level, jolly_level_t sec, int crit)
{
    return (jolly_alarm_obs_t){ .level = level, .sec = sec, .crit = crit };
}

static const jolly_alarm_obs_t OK = { JLV_OK, JLV_OK, 0 };

static void new_crit_alarms_then_calms(void)
{
    jolly_alarm_t a;
    jolly_alarm_init(&a);
    jolly_alarm_feed(&a, &OK, 0, 180);                          /* boot: all fine */
    assert(jolly_alarm_mood(&a, 0) == JM_TRANQUILO);
    jolly_alarm_obs_t backup = obs(JLV_CRIT, JLV_OK, 1);
    jolly_alarm_feed(&a, &backup, 10 * S, 180);                 /* a new crit */
    assert(jolly_alarm_mood(&a, 10 * S) == JM_ALERTA);
    assert(jolly_alarm_active(&a, 10 * S));
    for (int64_t t = 13 * S; t < 190 * S; t += 3 * S) {         /* polled every 3 s: still the same crit */
        jolly_alarm_feed(&a, &backup, t, 180);
    }
    assert(jolly_alarm_mood(&a, 10 * S + 180 * S - 1) == JM_ALERTA);
    assert(jolly_alarm_mood(&a, 10 * S + 180 * S) == JM_TRANQUILO);   /* 3 min, then calm */
    assert(!jolly_alarm_active(&a, 10 * S + 180 * S));
    jolly_alarm_feed(&a, &backup, 5 * HOUR, 180);               /* days of the same crit: no re-alarm */
    assert(jolly_alarm_mood(&a, 5 * HOUR + 30 * MIN) == JM_TRANQUILO);
}

static void reminders(void)
{
    jolly_alarm_t a;
    jolly_alarm_init(&a);
    jolly_alarm_feed(&a, &OK, 0, 180);
    jolly_alarm_obs_t backup = obs(JLV_CRIT, JLV_OK, 1);
    jolly_alarm_feed(&a, &backup, 0, 180);
    int64_t end = 180 * S;
    assert(jolly_alarm_mood(&a, end + HOUR - 1) == JM_TRANQUILO);
    assert(jolly_alarm_mood(&a, end + HOUR) == JM_SORPRENDIDO);         /* a glance an hour later */
    assert(jolly_alarm_mood(&a, end + HOUR + JOLLY_REMIND_MS - 1) == JM_SORPRENDIDO);
    assert(jolly_alarm_mood(&a, end + HOUR + JOLLY_REMIND_MS) == JM_TRANQUILO);   /* short */
    assert(jolly_alarm_mood(&a, end + 2 * HOUR + 1) == JM_SORPRENDIDO);  /* and every hour */
    jolly_alarm_feed(&a, &OK, end + 2 * HOUR + 2 * S, 180);             /* cleared: no more */
    assert(jolly_alarm_mood(&a, end + 3 * HOUR) == JM_TRANQUILO);

    /* A standing security crit reminds with espía. */
    jolly_alarm_init(&a);
    jolly_alarm_feed(&a, &OK, 0, 180);
    jolly_alarm_obs_t intrusion = obs(JLV_CRIT, JLV_CRIT, 1);
    jolly_alarm_feed(&a, &intrusion, 0, 180);
    assert(jolly_alarm_mood(&a, JOLLY_ALARM_SEC_S * S + HOUR) == JM_ESPIA);
}

static void security_alarms_longer(void)
{
    jolly_alarm_t a;
    jolly_alarm_init(&a);
    jolly_alarm_feed(&a, &OK, 0, 180);
    jolly_alarm_obs_t intrusion = obs(JLV_CRIT, JLV_CRIT, 1);
    jolly_alarm_feed(&a, &intrusion, 0, 180);
    assert(JOLLY_ALARM_SEC_S == 600);
    assert(jolly_alarm_mood(&a, 600 * S - 1) == JM_ALERTA);   /* 10 min */
    assert(jolly_alarm_mood(&a, 600 * S) == JM_TRANQUILO);
}

static void what_counts_as_new(void)
{
    jolly_alarm_t a;
    jolly_alarm_init(&a);
    jolly_alarm_feed(&a, &OK, 0, 180);
    jolly_alarm_obs_t one = obs(JLV_CRIT, JLV_OK, 1), two = obs(JLV_CRIT, JLV_OK, 2);
    jolly_alarm_feed(&a, &one, 0, 180);
    jolly_alarm_feed(&a, &one, HOUR, 180);
    assert(jolly_alarm_mood(&a, HOUR) == JM_TRANQUILO);
    jolly_alarm_feed(&a, &two, HOUR + 3 * S, 180);           /* a second crit: alarms again */
    assert(jolly_alarm_mood(&a, HOUR + 3 * S) == JM_ALERTA);
    jolly_alarm_feed(&a, &one, HOUR + 400 * S, 180);         /* one of them cleared: not new */
    jolly_alarm_feed(&a, &one, HOUR + 403 * S, 180);
    assert(jolly_alarm_mood(&a, HOUR + 403 * S) == JM_TRANQUILO);
    jolly_alarm_feed(&a, &two, HOUR + 500 * S, 180);         /* another one: new */
    assert(jolly_alarm_mood(&a, HOUR + 500 * S) == JM_ALERTA);

    /* An ops crit that becomes a security crit too: the 10 min alarm. */
    jolly_alarm_init(&a);
    jolly_alarm_feed(&a, &OK, 0, 180);
    jolly_alarm_feed(&a, &one, 0, 180);
    jolly_alarm_obs_t plus_sec = obs(JLV_CRIT, JLV_CRIT, 1);
    jolly_alarm_feed(&a, &plus_sec, 200 * S, 180);
    assert(jolly_alarm_mood(&a, 200 * S + 599 * S) == JM_ALERTA);

    /* A warning isn't an alarm; the level reaching crit is, even before the count says so. */
    jolly_alarm_init(&a);
    jolly_alarm_feed(&a, &OK, 0, 180);
    jolly_alarm_obs_t warn = obs(JLV_WARN, JLV_OK, 0), crit0 = obs(JLV_CRIT, JLV_OK, 0);
    jolly_alarm_feed(&a, &warn, S, 180);
    assert(jolly_alarm_mood(&a, S) == JM_TRANQUILO);
    jolly_alarm_feed(&a, &crit0, 2 * S, 180);
    assert(jolly_alarm_mood(&a, 2 * S) == JM_ALERTA);

    /* A crit that clears and comes back is new. */
    jolly_alarm_feed(&a, &OK, 3 * S, 180);
    assert(jolly_alarm_mood(&a, 3 * S) == JM_TRANQUILO);
    jolly_alarm_feed(&a, &one, 4 * S, 180);
    assert(jolly_alarm_mood(&a, 4 * S) == JM_ALERTA);
}

static void boot(void)
{
    /* A crit already open at boot: 30 s at most, then calm. */
    jolly_alarm_t a;
    jolly_alarm_init(&a);
    jolly_alarm_obs_t backup = obs(JLV_CRIT, JLV_OK, 1);
    jolly_alarm_feed(&a, &backup, 7 * S, 180);
    assert(jolly_alarm_mood(&a, 7 * S) == JM_ALERTA);
    assert(jolly_alarm_mood(&a, 7 * S + JOLLY_ALARM_BOOT_S * S - 1) == JM_ALERTA);
    assert(jolly_alarm_mood(&a, 7 * S + JOLLY_ALARM_BOOT_S * S) == JM_TRANQUILO);
    assert(JOLLY_ALARM_BOOT_S == 30);
    /* ... and a security one too. */
    jolly_alarm_init(&a);
    jolly_alarm_obs_t intrusion = obs(JLV_CRIT, JLV_CRIT, 1);
    jolly_alarm_feed(&a, &intrusion, 0, 180);
    assert(jolly_alarm_mood(&a, 30 * S) == JM_TRANQUILO);
    /* A shorter configured alarm still wins at boot. */
    jolly_alarm_init(&a);
    jolly_alarm_feed(&a, &backup, 0, 10);
    assert(jolly_alarm_mood(&a, 10 * S) == JM_TRANQUILO);
    /* Configured to 0: never alarms, still reminds. */
    jolly_alarm_init(&a);
    jolly_alarm_feed(&a, &OK, 0, 0);
    jolly_alarm_feed(&a, &backup, S, 0);
    assert(jolly_alarm_mood(&a, S) == JM_TRANQUILO);
    assert(jolly_alarm_mood(&a, S + HOUR) == JM_SORPRENDIDO);
}

static void alarm_seconds(void)
{
    assert(jolly_alarm_s_ok(0) && jolly_alarm_s_ok(180) && jolly_alarm_s_ok(3600));
    assert(!jolly_alarm_s_ok(-1) && !jolly_alarm_s_ok(3601));
    assert(JOLLY_ALARM_DEFAULT_S == 180);
}

int main(void)
{
    new_crit_alarms_then_calms();
    reminders();
    security_alarms_longer();
    what_counts_as_new();
    boot();
    alarm_seconds();
    return 0;
}
