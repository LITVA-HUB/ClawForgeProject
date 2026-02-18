# Stage 4 — Production Foundation

## Added in Stage 4

1. **Gateway auth**
   - Config: `gateway.auth.mode` = `off|token`
   - Config: `gateway.auth.tokenEnv` (env with Bearer token)
   - Applies to all `/api/*` endpoints.
   - `/health` stays public.

2. **Telegram access control (`dmPolicy`)**
   - `open` — allow all DMs
   - `allowlist` — only `telegram.allowFrom`
   - `pairing` — unknown users get pairing code; approve via CLI
   - `disabled` — reject all Telegram messages
   - Pairing state: `state/telegram/pairing.json`

3. **Message queue safety**
   - Per-session mutex/queue in AgentEngine
   - Prevents concurrent race for same `sessionKey`
   - Timeout via `gateway.messageQueueTimeoutMs`
   - Clear busy error message returned on timeout

4. **Generic inbound webhook**
   - `POST /api/inbound`
   - Body: `channel`, `peerId`, `text`, optional `systemEvent`
   - Routed into AgentEngine using `api.dmScope`

## Config example

```json
{
  "gateway": {
    "auth": {
      "mode": "token",
      "tokenEnv": "CLAWFORGE_GATEWAY_TOKEN"
    },
    "messageQueueTimeoutMs": 15000
  },
  "telegram": {
    "dmPolicy": "pairing"
  },
  "api": {
    "dmScope": "per-channel-peer"
  }
}
```

## CLI pairing

```bash
./build/nexaclaw pairing list --config config/config.json
./build/nexaclaw pairing approve ABC123 --config config/config.json
```

## curl examples

```bash
# public health
curl -s http://127.0.0.1:18890/health

# auth-protected status
curl -s http://127.0.0.1:18890/api/status \
  -H "Authorization: Bearer $CLAWFORGE_GATEWAY_TOKEN"

# generic inbound webhook
curl -s http://127.0.0.1:18890/api/inbound \
  -H "Authorization: Bearer $CLAWFORGE_GATEWAY_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"channel":"webhook","peerId":"u42","text":"ping"}'
```
