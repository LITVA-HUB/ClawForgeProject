# Self-hosting NexaClaw

This guide covers production-grade self-hosting patterns for NexaClaw. The defaults are tuned for local development. For operator use, work through each section.

## Build

```bash
git clone https://github.com/LITVA-HUB/ClawForgeProject.git
cd ClawForgeProject
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binary is `build/nexaclaw`. No runtime dependencies beyond the OS.

## Configuration

Copy and edit the example config:

```bash
cp config/config.example.json config/config.json
$EDITOR config/config.json
```

Key decisions:

| Setting | Default | Recommendation |
|---------|---------|----------------|
| `gateway.auth.mode` | `"off"` | Set to `"token"` if exposed beyond localhost |
| `http.host` | `"127.0.0.1"` | Keep loopback unless you add a reverse proxy |
| `rateLimit.maxRequests` | `120` | Lower for single-user; raise for team use |
| `audit.enabled` | `true` | Keep enabled; rotate the JSONL file periodically |
| `telegram.dmPolicy` | `"open"` | Set to `"pairing"` or `"allowlist"` for production |

## Auth token setup

Enable token auth:

```json
"gateway": {
  "auth": {
    "mode": "token",
    "tokenEnv": "NEXACLAW_GATEWAY_TOKEN"
  }
}
```

Set the token in the environment:

```bash
export NEXACLAW_GATEWAY_TOKEN="$(openssl rand -hex 32)"
```

All API requests then require:

```
Authorization: Bearer <token>
```

## State directory

NexaClaw writes state to `./state/` by default:

```
state/sessions/     — JSONL transcripts per session
state/cron/         — cron job definitions and run history
state/audit/        — audit event log (JSONL)
state/models/       — auth profile store
state/telegram/     — Telegram pairing state
```

Recommended: mount `state/` on a persistent volume and back up regularly.

## Reverse proxy (optional)

If you expose NexaClaw to a local network, put it behind a reverse proxy (nginx, Caddy) and add TLS. NexaClaw itself does not terminate TLS.

Minimal nginx snippet:

```nginx
location /nexaclaw/ {
    proxy_pass http://127.0.0.1:18890/;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
}
```

## systemd

A unit file is provided:

```bash
sudo cp deploy/nexaclaw.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable nexaclaw
sudo systemctl start nexaclaw
```

Edit `deploy/nexaclaw.service` to set the correct `WorkingDirectory` and `EnvironmentFile` path before copying.

## macOS launchd

```bash
cp deploy/com.nexaclaw.agent.plist ~/Library/LaunchAgents/
launchctl load ~/Library/LaunchAgents/com.nexaclaw.agent.plist
```

## Health check

```bash
curl -s http://127.0.0.1:18890/health
# {"status":"ok"}
```

Use this in uptime monitors or systemd `ExecStartPost`.

## Audit log rotation

The audit JSONL file grows unboundedly. Add logrotate or a cron job:

```bash
# /etc/logrotate.d/nexaclaw
/path/to/state/audit/events.jsonl {
    daily
    rotate 30
    compress
    missingok
    notifempty
    copytruncate
}
```

## Security checklist

- [ ] `gateway.auth.mode` set to `"token"`
- [ ] `NEXACLAW_GATEWAY_TOKEN` set via environment, not config file
- [ ] `http.host` is `127.0.0.1` or behind a trusted reverse proxy
- [ ] `telegram.dmPolicy` is `"pairing"` or `"allowlist"`, not `"open"`
- [ ] `toolsPolicy` deny list includes `exec` for untrusted channels
- [ ] `state/` directory is not world-readable
- [ ] Audit log rotation is configured
- [ ] API key env vars are not in config.json (use `apiKeyEnv` to reference env names, not values)
