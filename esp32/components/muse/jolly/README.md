# Jolly, the Mac Mini's guardian

Jolly is the LCDWIKI ES3C40P in landscape (480x320). Its eyes take the place of
Muse's avatar and show how the Mac Mini is doing, and four status pages follow
them: Mac Mini, Apps, Red and Seguridad. It reads everything from the
Guardián (`saxo2002AC/warroom-guardian`), a read-only service on the Mini, through
the frozen contract v1. The spec is the guardian plan
(`warroom-guardian/docs/plan-guardian-v1.md`, workstream B) and the v3 landscape
mockup (`warroom-guardian/docs/mockup-v3-horizontal.html`). It's built with
`CONFIG_MUSE_JOLLY`, which only the ES3C40P's overlay turns on.

## Files

| File | What | Tested by |
|---|---|---|
| `../boards/es3c40p_orient.c` | landscape MADCTL and touch remap | `tests/jolly/test_orient.c` |
| `jolly_mood.c` | the mood, first match wins (plan §1.9) | `tests/jolly/test_mood.c` |
| `jolly_eyes.c` | the eyes and mouth: saccades, blinks, lids, hearts | simulator |
| `jolly_mouth.c` | the mouth for each mood, and the talking opening | `tests/jolly/test_mouth.c` |
| `jolly_glance.c` | strict contract parsers, ETag check, link state (§1.8) | `tests/jolly/test_glance.c` |
| `jolly_client.c` | the poll task, NVS config, console | device, `tools/jolly/fake_guardian.py` |
| `jolly_text.c` | the pages' Spanish strings | `tests/jolly/test_text.c` |
| `jolly_pages.c` | the four status pages and the chips | simulator |
| `../muse_url.c`, `../muse_resolve.c` | `http://host:port` and `.local` names by mDNS | `tests/jolly/test_url.c` |
| `../fonts/jolly_font_*.c` | Montserrat with Latin-1 (`tools/muse/gen_jolly_fonts.sh`) | `test_fonts_cover_spanish` |

`tests/test_jolly_host.py` runs the C tests. The glance test needs
`managed_components/` (after an `idf.py reconfigure`) for cJSON.

## Console (serial only, never BLE)

```
>grd_url=http://Mac-mini-de-acserver.local:3025    the Guardián (or http://IPv4:port)
>grd_key=<key>                                     X-Guardian-Key; never printed or logged
>grd                                               URL, key length, link, whether a glance arrived
>lcd / >lcd_orient=0|1 / >lcd_madctl=S,X,Y / >lcd_touchlog=1
>tts_url=http://Mac-mini-de-acserver.local:3024 / >tts_key=...
```

An empty value clears a setting. A line in the `grd_`, `tts_`, `sdk_token` or
`lcd_` family that no handler took is refused (`@error invalid secret command`)
and never reaches the setup commands' log.

## Security conditions (Diana's review)

- **R14:** the console refuses malformed `grd_`, `tts_`, `sdk_token` and `lcd_`
  lines (`muse_secret.c`). `run_command` never echoes the value of a secret.
  The keys print only as their length, local copies are wiped, and the serial
  line buffer is wiped before it's freed.
- **R15:** `jolly_client.c` keeps redirects off and enforces each endpoint's body
  cap (glance 1536, apps 3072, events 4096, series 2048 bytes) on
  Content-Length and while reading. It requires `application/json`.
  `jolly_glance.c` parses strictly: `v == 1`, typed fields, known enums,
  clamped integers, UTF-8-safe fixed buffers without control characters,
  arrays up to the contract's length, and series exactly 60 or 24 long. An ETag
  is only sent back if it's 1 to 32 bytes of printable ASCII.
- **R16:** no label uses recolour (`test_no_label_recolor`). Server text arrives
  already sanitized by the parsers. An error response is logged as at most
  64 sanitized bytes, at debug level.

## Simulator

```
cmake -S esp32/simulator -B <build> -G Ninja -DMUSE_SIM_JOLLY=ON
MUSE_SIM_BOARD=es3c40p MUSE_SIM_JOLLY_DATA=crit <build>/muse_simulator --headless \
    --scenario <file with tile=1> --screenshot out.ppm
```

Scenario keys: `face=` drives the voice moods, `jolly=ok|crit|spy|quiet|down|none`
sets the stub's data (crit then ok gives feliz), and `pet=1` pats the eyes (cariño).
`MUSE_SIM_BOARD=es3c40p` gives the 480x320 profile. `MUSE_SIM_JOLLY_DATA=ok|crit`
fills the client stub with the fixtures' values. `MUSE_SIM_JOLLY_LINK=down`
gives no link, and `tile=N` in a scenario shows the tileview's column N.

## The mouth

It sits under the eyes, in their colour, and changes with the mood behind the
same blink. The eyes keep their size and spacing, 26 px higher.

| Mood | Mouth |
|---|---|
| tranquilo | a small soft smile (arc) |
| feliz, cariño | a bigger smile |
| hablando | opens and closes with the reply's loudness (the level `muse_voice` sets from the PCM it plays), or a natural syllable pattern between words |
| escuchando | a small "o" |
| pensando | a short flat line, to one side |
| espía | a flat line |
| alerta | a big "O", trembling with the eyes |
| durmiendo | a tiny line and a swelling snore bubble |
| desconectado | a flat grey line |

## Touch and motion

- **Touch on the face** (`jolly_gesture.c`): a tap pets Jolly. A hold of 350 ms
  without moving more than 20 px is push-to-talk, the BOOT key's path through
  `muse_input_touch_talk()`, until the finger lifts; Jolly shows escuchando
  meanwhile. Moving further first is a swipe to another page, so neither.
- **IMU** (`jolly_imu.c`, `jolly_motion.c`): an optional MPU-6050 (GY-521) on the
  I2C header (3V3, GND, SDA IO16, SCL IO15; 0x68 beside the touch controller's
  0x38 and the codec's 0x18), probed at start. Without it nothing changes. With it,
  a 3 KB PSRAM-stack task reads the accelerometer at 100 Hz (gyro off, ±8 g).
  `>imu` prints whether it's there, the last reading in g, and the mood it asks for.

| Motion | Mood | For |
|---|---|---|
| shake (acceleration spread > 450 mg for 400 ms) | mareado | 3 s |
| freefall (< 0.3 g for 120 ms) | asustado | until the landing, 1.5 s at most |
| landing (> 2 g within 1.5 s of a fall) | aliviado | 2.5 s |
| knock (> 2.5 g for ≤ 30 ms, idle or touched) | enojado | 2 s |
| face-down (gravity on −Z for 2 s) | durmiendo | until it's turned up |

The arbiter puts these after voice and a pat. They win over no link, espía,
feliz and quiet hours, but not over a current crit: security still wins over
play. Face-down assumes the GY-521 lies flat, chip side towards the screen's
front. `>imu` shows which way gravity reads once it's fitted.

## Decisions

- **No clock:** the firmware has no SNTP, so the status bar shows Wi-Fi and the
  chip but no time. Every "hace N" uses the Guardián's own timestamps
  (`glance.ts` minus the event's `t`, or the server's `age_s` fields).
  "actualizado hace N s" is the board's own time since the last 200 or 304.
- **Compact layout:** landscape at 320 px of height takes Muse's compact
  layout (`short_landscape`). The full layout needs about 466 px.
- **One chip:** the status bar is the same on every page, as the plan says.
  The mockup's per-page chips ("Internet OK", "SSH 0 fallos") aren't used.
- **mDNS:** `.local` names are resolved through lwIP's one-shot query
  (`CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES`), so there's no mdns component.

## Not in v1

- **Eyes:** escuchando's waves, pensando's dots, hablando's EQ bars, the mood
  label under the eyes, and the radar sweep.
- **Pages:** pulsing status dots, sliding new events, the dashed tx line, and an
  on-screen talk button in landscape. BOOT does push-to-talk.

## Status

Built and host-tested. The simulator renders the eyes and all four pages.
**Not yet on hardware:** orientation, touch corners, internal RAM at idle
(plan gate ≥16 KB: read `UI built: free internal` and `ready: ... internal`),
and the live link against the fake or the real Guardián.
