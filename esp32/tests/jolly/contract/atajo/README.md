# Contrato jolly-atajo v1.1 (borrador para el visto de Fernando, Miguel y Valentín)

- **Base:** plan §3 (v1 congelado). **v1.1** cambia solo el transporte de la autenticación: HMAC por petición en vez de la clave en claro (`X-Jolly-Key`), y respuesta firmada. El JSON de respuesta sigue siendo `v:1` con las mismas 8 claves.
- **Por qué:** no hay acceso al router, así que no hay reserva DHCP (C11). Se acepta la URL por mDNS solo con respuesta firmada (C11 → C15). Además, C14: con `action=local`, `transcript` va siempre `""`.
- **Estado:** fixtures generados por `make_fixtures.py`. Pasan a `jolly-atajo/contract/` en A1 y se copian idénticos a `esp32/tests/jolly/contract/`.

## Base
`http://Mac-mini-de-acserver.local:<PORT>` (PORT de `npm run port:allocate` en A6; el firmware guarda la URL entera en NVS).

## `GET /health`
Pasa la puerta IP/MAC y no pide autenticación. `200 {"ok":true,"v":1,"stt":"<engine>:<model>","warm":true}`. No va firmada.

## `POST /v1/ask`

| Cabecera | Regla |
|---|---|
| `X-Jolly-Ts` | segundos Unix, `^[0-9]{10}$`, dentro de **±60 s** del reloj del Mini. El board necesita SNTP. |
| `X-Jolly-Nonce` | 16 bytes aleatorios en hex minúscula, `^[0-9a-f]{32}$`. No se repite: el servidor guarda los nonces vistos durante 120 s (tope 4096; lleno → 503 `busy`). |
| `X-Jolly-Auth` | hex minúscula de `HMAC-SHA256(K, "POST|/v1/ask|<ts>|<nonce>|<sha256hex(body)>")`. `K` = los **32 bytes** decodificados de la clave de 64 hex (no la cadena ASCII). Se compara con `hmac.compare_digest`. |
| `Content-Type` | `audio/L16; rate=16000; channels=1` (parámetros en cualquier orden, sin distinguir mayúsculas; otro tipo → 400) |
| `Content-Length` | obligatorio, exactamente `^[0-9]{1,6}$`, una sola vez, par, `9600 ≤ N ≤ 480000`. `Transfer-Encoding` presente → 400 (C1) |
| `X-Jolly-Turn` | opcional, `^[0-9]{1,10}$`, solo para correlacionar logs |
| Body | PCM s16le mono 16 kHz crudo, sin cabecera WAV, con el pre-roll |

`K` es distinta de las claves de muse-voz y del guardián; el servicio no arranca si coincide con alguna (C3).

### Respuesta 200
`Content-Type: application/json; charset=utf-8`, `Content-Length`, `Connection: close`, cuerpo ≤ 1024 B, y

`X-Jolly-Sig` = hex de `HMAC-SHA256(K, "RESP|<nonce>|<status>|<sha256hex(body)>")`

El board verifica la firma **antes** de parsear. Firma ausente o inválida → forward con audio + breaker++ (igual que JSON malo).

Cuerpo: las 8 claves, en este orden, serializadas con `ensure_ascii=False` y separadores `,` `:` sin espacios.

| Campo | Valores |
|---|---|
| `v` | `1` |
| `action` | `local` \| `forward` |
| `intent` | local: `hora` `fecha` `estado` `apps` `seguridad` `respaldo` `clima`; forward: `""` |
| `text` | local: 1–280 B UTF-8, sin controles C0/C1/7F ni `Cf`/`Co`/`Cn` (C9), sin markdown; forward: `""` |
| `reason` | local: `""`; forward: `no_match` `no_speech` `low_confidence` `other_language` `slow` `data_unavailable` `disabled` `error` |
| `transcript` | **local: siempre `""` (C14)**; forward: 0–400 B, `""` si no hubo resultado |
| `stt_ms`, `total_ms` | 0–60000 |

### Errores
`{"ok":false,"error":"<code>"}`, en este orden de chequeo:

1. **403 `forbidden`** (IP/MAC). Sin leer el cuerpo.
2. **401 `bad_auth`** si falta alguna de `X-Jolly-Ts/Nonce/Auth`, tienen mal formato, el ts está fuera de ±60 s o el nonce ya se vio. Sin leer el cuerpo.
3. 404/405. Sin leer el cuerpo.
4. **400 `bad_request`** (framing, tipo, longitud impar, corta, duplicada o con formato malo, chunked) y **413 `too_large`**. Solo con las cabeceras.
5. **429 `rate_limited`** (>20/min) y **503 `busy`** (`Retry-After: 1`). Solo con las cabeceras.
6. Se lee el cuerpo (exactamente `Content-Length`, plazo total 3 s, 1 s por lectura) y se verifica el HMAC. Si no casa → **401 `bad_auth`** y el nonce queda quemado igual.
7. **500 `internal`**.

Todas las respuestas a una petición con nonce bien formado llevan `X-Jolly-Sig`. El board solo la exige en los 200.

**Punto abierto para Diana (C1):** con el hash del cuerpo dentro del HMAC, el 401 por firma mala llega **después** de leer el cuerpo. Mitigación: solo se lee si la MAC pasó y las tres cabeceras de autenticación están bien formadas y frescas, con tope 480 KB y plazo 3 s. El 403 y el 401 por cabeceras siguen sin leer nada.

### Obligaciones del servidor
Contesta ≤1200 ms después del último byte del cuerpo; si el STT no terminó a los 1100 ms, `forward/slow` y descarta el resultado (que nunca se registra, C16). Máximo 8 conexiones (6 para peers LAN desconocidos, C2), STT de concurrencia 1, XFF ignorado.

### Obligaciones del cliente
Las del plan §3, más: firmar cada petición con ts y nonce nuevos; verificar `X-Jolly-Sig` en los 200; nunca imprimir la clave, la firma ni el `text`/`transcript` con `fast_log=0` (C13).

## Fixtures

| Archivo | Caso |
|---|---|
| `ask.local.hora.json`, `ask.local.estado.json` | local, `transcript:""` |
| `ask.forward.no_match.json`, `ask.forward.no_speech.json`, `ask.forward.slow.json` | forward |
| `ask.worstcase.json` (924 B) | 400 B de `"` escapadas + reason más largo + enteros máximos |
| `ask.worstcase_local.json` (395 B) | 280 B de UTF-8 de 2 bytes |
| `auth.vectors.json` | vector de prueba **público** de la petición y de la respuesta firmada |
