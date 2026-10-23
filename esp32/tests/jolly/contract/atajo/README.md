# jolly-atajo contract v1.1 — PROVISIONAL (firmware side)

Written by Miguel from the frozen v1 in `plan-atajo-rapido.md` §3, with the
changes below. **Provisional** until Fernando's `jolly-atajo/contract/` exists
and the three of us (Fernando, Miguel, Valentín) sign it; then these files are
replaced by identical copies of his.

## What changed from v1

1. **Parallel mode (Andrés, D1).** The board keeps streaming the note to Muse
   live, as before; `/v1/ask` runs alongside it. A `local` answer makes the
   board cancel and suppress Muse's turn. `fast_fwd` and the text forward are
   gone (D2 is moot), so C15's "blocking" use doesn't apply; the response
   signature below is still required (Diana C15, "recommended in audio mode").
   Diana's C12 (no audio leaks) doesn't apply: in parallel mode the audio
   always reaches Muse, by Andrés's choice.
2. **No key on the wire (replaces `X-Jolly-Key`; Diana C11 without a DHCP
   reservation).** Andrés has no router access, so the URL stays
   `http://Mac-mini-de-acserver.local:<PORT>` and anyone on the LAN could
   answer that mDNS query. So the board never sends the key:

   | Request header | Value |
   |---|---|
   | `X-Jolly-Ts` | unix seconds, decimal (the board's SNTP clock; no clock, no ask) |
   | `X-Jolly-Nonce` | 32 lowercase hex, random per request |
   | `X-Jolly-Auth` | lowercase hex `HMAC-SHA256(K, "POST|/v1/ask|<ts>|<nonce>|<hex sha256(body)>")` |

   `K` is the 32 bytes the 64-hex key stands for. The server: rejects
   `|now − ts| > 60 s` (401 `bad_auth`), rejects a nonce seen in the last
   120 s (401 `replay`), compares with `hmac.compare_digest`, and only then
   reads the body (C1: framing checks stay header-only; the body's hash is
   checked after reading it, before the STT).

3. **Signed answers (Diana C15).** Every 200 carries
   `X-Jolly-Sig: hex HMAC-SHA256(K, "<nonce>|" + body)`. The board checks it
   before it acts on the answer; a missing or wrong signature counts as an
   error (Muse's turn goes on, breaker + 1).
4. **No transcript with `local` (Diana C14).** `transcript` is `""` whenever
   `action` is `local`; the 8 keys stay.

Everything else is v1 as frozen: the 8 keys, enums, sizes (`text` 1–280 B for
`local`, `transcript` ≤ 400 B, body ≤ 1024 B), `Content-Type:
audio/L16; rate=16000; channels=1`, `Content-Length` 9600–480000 and even,
`Connection: close`, and the ≤ 1200 ms answer.

## Board rules (C13)

The board treats as an error (Muse goes on, breaker + 1): no 200, not
`application/json`, a body over 1024 B, a bad or missing `X-Jolly-Sig`, bad
JSON, `v != 1`, an unknown `action`/`intent`/`reason`, a missing key or one of
the wrong type, `local` with an empty `text`, `text` over 280 B, invalid UTF-8,
or a `text` with controls, C1 (U+0080–9F) or format characters (bidi,
zero-width). Unknown extra keys are ignored. `transcript` is never shown and
only logged with `>fast_log=1`.

## Fixtures

`ask.local.hora.json`, `ask.local.estado.json`, `ask.forward.no_match.json`,
`ask.forward.no_speech.json`, `ask.worstcase.json` (forward, 400 B
transcript), `ask.worstcase.local.json` (280 B text).
