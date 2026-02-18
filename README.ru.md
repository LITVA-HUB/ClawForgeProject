# ClawForge

**Практичный self-hosted AI gateway на C++** (архитектурно в духе OpenClaw, но как лёгкое ядро).

ClawForge — это локальный control plane: сессии, tools, cron, Telegram baseline, realtime-события, очередь задач, security-ограничения и ops-скрипты.

- 🇬🇧 English README: [README.md](./README.md)
- Матрица parity: [docs/PARITY_ROADMAP.md](./docs/PARITY_ROADMAP.md)
- Документация по стадиям: [STAGE4](./docs/STAGE4.md) · [STAGE5](./docs/STAGE5.md) · [STAGE6](./docs/STAGE6.md) · [STAGE7](./docs/STAGE7.md) · [STAGE8](./docs/STAGE8.md)

---

## Что уже работает

- HTTP API gateway: `/health`, `/api/status`, `/api/message`, `/api/inbound`, `/api/tools`, `/api/sessions`
- Realtime SSE stream: `/api/events/stream`
- Cron-движок: `every` / `at` / `cron` + validate + run-now
- Session store + JSONL-транскрипты
- Scoped policy для tools (`global` / `channels` / `peers`)
- Telegram baseline + pairing policy/approve
- Task lane API (`/api/tasks`) с timeout/cancel baseline
- Security baseline: auth token mode, rate limit по источнику, audit JSONL
- Browser relay baseline (`status/open`) + честный diagnostic stub для snapshot
- RU/EN CLI UX + doctor + smoke/benchmark scripts

---

## Быстрый старт (5 минут)

### Что нужно

- macOS/Linux
- C++20 компилятор (AppleClang/clang/g++)
- CMake >= 3.20
- `curl`, `python3`, `bash`

### Запуск

```bash
cd ~/PycharmProjects/ClawForgeProject
cp config/config.example.json config/config.json

# Нужен для ответов через LLM
export OPENAI_API_KEY="<твой_ключ>"

scripts/bootstrap.sh
./build/clawforge run --config config/config.json
```

Проверка:

```bash
curl -s http://127.0.0.1:18890/health
```

---

## CLI шпаргалка

```bash
./build/clawforge --help
./build/clawforge --doctor --config config/config.json
./build/clawforge status --config config/config.json
./build/clawforge cron list --config config/config.json
./build/clawforge tools list --config config/config.json
./build/clawforge pairing list --config config/config.json
./build/clawforge pairing approve <code> --config config/config.json
```

Язык CLI:

```bash
./build/clawforge --lang ru --help
./build/clawforge --lang en --help
```

---

## Быстрые примеры API

### Опциональная auth-защита

Если `gateway.auth.mode = "token"`, добавляй заголовок:

```bash
-H "Authorization: Bearer $CLAWFORGE_GATEWAY_TOKEN"
```

### База

```bash
curl -s http://127.0.0.1:18890/api/status
curl -s -X POST http://127.0.0.1:18890/api/message \
  -H 'Content-Type: application/json' \
  -d '{"sessionKey":"main","text":"Привет"}'
```

### Inbound router

```bash
curl -s -X POST http://127.0.0.1:18890/api/inbound \
  -H 'Content-Type: application/json' \
  -d '{"channel":"telegram","peerId":"123","text":"/status"}'
```

### Realtime события (SSE)

```bash
curl -N http://127.0.0.1:18890/api/events/stream
```

### Browser baseline

```bash
curl -s http://127.0.0.1:18890/api/browser/status
curl -s -X POST http://127.0.0.1:18890/api/browser/open \
  -H 'Content-Type: application/json' -d '{"url":"https://example.com"}'
curl -s -X POST http://127.0.0.1:18890/api/browser/snapshot \
  -H 'Content-Type: application/json' -d '{"url":"https://example.com"}'
```

### Задачи

```bash
curl -s -X POST http://127.0.0.1:18890/api/tasks \
  -H 'Content-Type: application/json' \
  -d '{"channel":"api","peerId":"demo","text":"/status","timeoutMs":5000}'
curl -s http://127.0.0.1:18890/api/tasks
```

---

## Проверка качества

```bash
# полный smoke pipeline
scripts/smoke_full.sh

# stage-focused
scripts/smoke_stage5.sh
scripts/smoke_stage6.sh

# строгая сборка (warning == error)
cmake -S . -B build_warnings -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build_warnings -j

# быстрый перф-чек
scripts/benchmark_quick.sh 50
```

---

## Шаблоны сервисов

- systemd: `deploy/clawforge.service`
- launchd: `deploy/com.clawforge.agent.plist`

---

## Важные блоки конфига

Смотри `config/config.example.json`.

Ключевые секции:

- `gateway.auth` — auth mode off/token
- `gateway.messageQueueTimeoutMs`
- `telegram.dmPolicy` — `open|allowlist|pairing|disabled`
- `api.dmScope` — `main|per-peer|per-channel-peer`
- `toolsPolicy.scopes` — policy на уровне global/channel/peer
- `browser` — backend + диагностика
- `taskLane` — очередь + timeout
- `rateLimit` — лимиты запросов по источнику
- `audit` — путь к JSONL audit trail

---

## Честные ограничения

- `browser/snapshot` пока диагностический stub (без реального DOM/скриншота)
- Telegram пока baseline, а не full multi-channel экосистема
- Нет Canvas/Nodes интеграции
- Нет полной внешней orchestration-модели subagents

Актуальный статус смотри в [docs/PARITY_ROADMAP.md](./docs/PARITY_ROADMAP.md).
