# ClawForge — практичный локальный gateway (C++)

ClawForge: HTTP API, сессии, tools, cron, baseline Telegram, baseline browser relay, async task lane и ops-скрипты.

- EN docs: [`README.md`](./README.md)
- Stage docs: [`docs/STAGE6.md`](./docs/STAGE6.md), [`docs/STAGE7.md`](./docs/STAGE7.md), [`docs/STAGE8.md`](./docs/STAGE8.md)
- Матрица parity: [`docs/PARITY_ROADMAP.md`](./docs/PARITY_ROADMAP.md)

## Быстрый старт

```bash
cd "пайчарм проджект"
scripts/bootstrap.sh
scripts/smoke_full.sh
./build/clawforge run --config config/config.json
```

## Главные endpoint'ы

- База: `/health`, `/api/status`, `/api/message`, `/api/inbound`, `/api/sessions`, `/api/tools`
- Browser relay baseline: `/api/browser/status`, `/api/browser/open`, `/api/browser/snapshot`
- Cron: `/api/cron/*`
- Tasks (Stage 7): `/api/tasks`, `/api/tasks/<id>`, `/api/tasks/<id>/cancel`

## Ops-скрипты

- `scripts/bootstrap.sh`
- `scripts/smoke_stage6.sh`
- `scripts/smoke_full.sh`
- `scripts/benchmark_quick.sh [N]`

## Шаблоны сервисов

- systemd: `deploy/clawforge.service`
- launchd: `deploy/com.clawforge.agent.plist`

## Миграция (Stage 5 -> 8)

- `toolsPolicy` теперь поддерживает scoped-схему (`scopes.global/channels/peers`).
- `security.requestsPerMinutePerSession` заменён на `rateLimit`.
- Добавлены блоки: `taskLane`, `audit`, `browser`.
- Legacy `toolsPolicy.allow/deny` остаётся совместимым.

## Известные ограничения

- Browser snapshot пока диагностический stub (без реального захвата DOM/картинки).
- Async task lane пока single-worker baseline.
- Нет Canvas/Nodes интеграции.
- Нет полноценной multi-channel plugin системы (кроме Telegram baseline).
