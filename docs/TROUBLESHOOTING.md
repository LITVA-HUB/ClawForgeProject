# Troubleshooting NexaClaw

Run the doctor first:

```bash
./build/nexaclaw --doctor --config config/config.json
```

It checks config validity, state directory accessibility, port availability, and API key presence.

---

## Build failures

### CMake version too old

```
CMake 3.20 or higher is required.
```

Install a newer CMake from https://cmake.org/download/ or via your package manager.

### Missing C++20 support

```
error: 'std::filesystem' is not a namespace-name
```

Upgrade your compiler: clang++ >= 12, g++ >= 10, or AppleClang >= 13.

### nlohmann/json or cpp-httplib fetch fails

CMake fetches these at configure time. If your network is restricted:

```bash
# Pre-download and set FETCHCONTENT_SOURCE_DIR
cmake -S . -B build \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/path/to/json \
  -DFETCHCONTENT_SOURCE_DIR_CPP_HTTPLIB=/path/to/cpp-httplib
```

---

## Runtime issues

### Port already in use

```
bind: address already in use
```

Change `http.port` in `config/config.json`, or find and stop the process using port 18890:

```bash
lsof -i :18890
```

### 401 Unauthorized on all requests

Auth token mode is enabled. Set the header:

```bash
curl -H "Authorization: Bearer $NEXACLAW_GATEWAY_TOKEN" http://127.0.0.1:18890/api/status
```

Or disable auth in config: `"gateway.auth.mode": "off"` (loopback only).

### 429 Too Many Requests

Rate limit exceeded. Either:
- Wait for the window to reset (default: 60 seconds)
- Raise `rateLimit.maxRequests` in config

### LLM responses return an error

1. Check the API key is set: `echo $OPENAI_API_KEY`
2. Check the model is reachable: `./build/nexaclaw models probe --config config/config.json`
3. Check the provider endpoint in `modelsConfig.providers`

### Telegram bot not responding

1. Confirm `telegram.enabled: true` in config
2. Check `TELEGRAM_BOT_TOKEN` is set and valid
3. Check `allowFrom` list — if non-empty, only listed user IDs receive responses
4. Try `dmPolicy: "open"` for initial testing

### Cron jobs not running

1. Check `cron.enabled: true`
2. Verify cron state exists: `./build/nexaclaw cron list --config config/config.json`
3. Check `enabled` field on each job: `./build/nexaclaw cron status --config config/config.json`
4. Check session target exists for jobs that have `sessionTarget`

### Browser commands return "not available"

The browser backend is `stub` by default. Switch to `native`:

```json
"browser": {
  "backend": "native"
}
```

For real browser control via OpenClaw: `"backend": "openclaw_cli"` and ensure `openclaw` binary is on PATH.

---

## Smoke tests

Run the safe baseline smoke test (no external credentials required):

```bash
# Start nexaclaw first, then:
scripts/smoke_stage5.sh
scripts/smoke_stage12_gateway_security.sh
```

Full smoke pipeline (requires a running instance):

```bash
scripts/smoke_full.sh
```

---

## Getting help

- Open a [bug report](https://github.com/LITVA-HUB/ClawForgeProject/issues/new?template=bug_report.md)
- Check [docs/CLI_PARITY.md](./CLI_PARITY.md) for command availability status
- Run `./build/nexaclaw --help` and `./build/nexaclaw --doctor`
