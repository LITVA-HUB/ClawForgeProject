# Stage 10 — OpenClaw deep parity sprint

## What was delivered

1. CLI audit against OpenClaw docs (`/opt/homebrew/lib/node_modules/openclaw/docs/cli/*.md`)
2. Real CLI feature additions (browser/cron/tools/config/logs/system/models)
3. Documentation polish and migration notes
4. QA smoke additions

## New/expanded CLI commands

## Browser
```bash
./build/clawforge browser status --config config/config.json
./build/clawforge browser open https://example.com --config config/config.json
./build/clawforge browser snapshot https://example.com --config config/config.json
```

## Cron
```bash
./build/clawforge cron list --config config/config.json
./build/clawforge cron validate --json '{"name":"ping","kind":"every","everyMs":60000,"sessionKey":"main","message":"/status"}' --config config/config.json
./build/clawforge cron add --json '{"name":"ping","kind":"every","everyMs":60000,"sessionKey":"main","message":"/status"}' --config config/config.json
./build/clawforge cron run <job-id> --config config/config.json
./build/clawforge cron rm <job-id> --config config/config.json
```

## Tools
```bash
./build/clawforge tools list --config config/config.json
./build/clawforge tools call shell.exec --json '{"command":"echo hello"}' --config config/config.json
```

## Config
```bash
./build/clawforge config get gateway.auth.mode --config config/config.json
./build/clawforge config set gateway.auth.tokenEnv CLAWFORGE_GATEWAY_TOKEN --config config/config.json
./build/clawforge config get api.dmScope --config config/config.json
./build/clawforge config set telegram.dmPolicy pairing --config config/config.json
./build/clawforge config get models.routing.current --config config/config.json
```

## Logs / System
```bash
./build/clawforge logs tail 100 --config config/config.json
./build/clawforge system event "maintenance: nightly sweep" --config config/config.json
```

## Models parity step
```bash
./build/clawforge models probe --config config/config.json
./build/clawforge models set-image openai/gpt-image-1 --config config/config.json
./build/clawforge image-fallbacks list --config config/config.json
./build/clawforge image-fallbacks add openrouter/stability/sdxl --config config/config.json
./build/clawforge image-fallbacks remove openrouter/stability/sdxl --config config/config.json
./build/clawforge image-fallbacks clear --config config/config.json
```

## Notes
- Browser snapshot remains baseline diagnostic (honest `partial` parity).
- `logs tail` reads configured audit log file.
- `system event` uses API path when server is up; otherwise falls back to local main-session system message append.
- Full parity matrix: `docs/CLI_PARITY.md`.
