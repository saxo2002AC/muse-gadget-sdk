"""Generate the jolly-atajo contract v1.1 fixtures (ask.*.json + auth.*.json).

The JSON body schema is v1 (8 keys, unchanged). v1.1 changes the transport auth:
per-request HMAC (X-Jolly-Ts / X-Jolly-Nonce / X-Jolly-Auth) and a signed
response (X-Jolly-Sig). Test key below is a PUBLIC test vector, never a real key.
Serialization is canonical: key order as in the contract, ensure_ascii=False,
separators=(",", ":"), UTF-8.
"""
import hashlib
import hmac
import json
from pathlib import Path

OUT = Path(__file__).resolve().parent
KEYS = ["v", "action", "intent", "text", "reason", "transcript", "stt_ms", "total_ms"]
TEST_KEY_HEX = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"


def body(**kw):
    d = {"v": 1, "action": "", "intent": "", "text": "", "reason": "", "transcript": "", "stt_ms": 0, "total_ms": 0}
    d.update(kw)
    assert list(d) == KEYS
    return json.dumps(d, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def sha(b):
    return hashlib.sha256(b).hexdigest()


def mac(msg):
    return hmac.new(bytes.fromhex(TEST_KEY_HEX), msg.encode("ascii"), hashlib.sha256).hexdigest()


fixtures = {
    # C14: with action=local the transcript is always "".
    "ask.local.hora": body(action="local", intent="hora", text="Son las tres y veinte de la tarde.",
                           stt_ms=262, total_ms=270),
    "ask.local.estado": body(action="local", intent="estado",
                             text="Ojo: 1 alerta crítica. Respaldo externo: hace 10 días.", stt_ms=255, total_ms=281),
    "ask.forward.no_match": body(action="forward", reason="no_match",
                                 transcript="cuéntame un cuento de piratas", stt_ms=270, total_ms=274),
    "ask.forward.no_speech": body(action="forward", reason="no_speech", stt_ms=0, total_ms=3),
    "ask.forward.slow": body(action="forward", reason="slow", stt_ms=1100, total_ms=1101),
    # Worst case for size: 400 B transcript of '"' (each escapes to 2 B) + longest reason + max ints.
    "ask.worstcase": body(action="forward", reason="data_unavailable", transcript='"' * 400,
                          stt_ms=60000, total_ms=60000),
    # Worst case for a local reply: 280 B of 2-byte UTF-8 + longest intent.
    "ask.worstcase_local": body(action="local", intent="seguridad", text="ñ" * 140, stt_ms=60000, total_ms=60000),
}
for name, b in fixtures.items():
    assert len(b) <= 1024, (name, len(b))
    (OUT / f"{name}.json").write_bytes(b)

# --- auth test vectors -------------------------------------------------------
pcm = bytes(range(256)) * 75  # 19200 B = 0.6 s, even, within 9600..480000
ts, nonce = "1792771200", "00112233445566778899aabbccddeeff"
req_msg = f"POST|/v1/ask|{ts}|{nonce}|{sha(pcm)}"
resp_body = fixtures["ask.local.hora"]
resp_msg = f"RESP|{nonce}|200|{sha(resp_body)}"
auth = {
    "note": "PUBLIC TEST VECTOR. key = 32 raw bytes decoded from key_hex (not the ASCII hex string).",
    "key_hex": TEST_KEY_HEX,
    "request": {
        "method": "POST", "path": "/v1/ask", "body": "bytes(range(256)) * 75 (19200 B)",
        "body_sha256": sha(pcm),
        "headers": {"X-Jolly-Ts": ts, "X-Jolly-Nonce": nonce, "Content-Type": "audio/L16; rate=16000; channels=1",
                    "Content-Length": str(len(pcm))},
        "signed_string": req_msg,
        "X-Jolly-Auth": mac(req_msg),
    },
    "response": {
        "status": 200, "body_fixture": "ask.local.hora.json", "body_sha256": sha(resp_body),
        "signed_string": resp_msg,
        "X-Jolly-Sig": mac(resp_msg),
    },
}
(OUT / "auth.vectors.json").write_text(json.dumps(auth, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
for p in sorted(OUT.glob("*.json")):
    print(f"{p.name:28s} {p.stat().st_size:5d} B  sha256={sha(p.read_bytes())[:16]}")
