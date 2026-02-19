# NexaClaw

**Практичный self-hosted AI gateway на C++** (архитектурно в духе OpenClaw, но как лёгкое ядро).

NexaClaw — это локальный control plane: сессии, tools, cron, Telegram baseline, realtime-события, очередь задач, security-ограничения и ops-скрипты.

- 🇬🇧 English README: [README.md](./README.md)
- Матрица parity: [docs/PARITY_ROADMAP.md](./docs/PARITY_ROADMAP.md)
- CLI parity таблица: [docs/CLI_PARITY.md](./docs/CLI_PARITY.md)
- Документация по стадиям: [STAGE4](./docs/STAGE4.md) · [STAGE5](./docs/STAGE5.md) · [STAGE6](./docs/STAGE6.md) · [STAGE7](./docs/STAGE7.md) · [STAGE8](./docs/STAGE8.md) · [STAGE10](./docs/STAGE10.md) · [STAGE11](./docs/STAGE11.md) · [STAGE12](./docs/STAGE12.md) · [STAGE13](./docs/STAGE13.md) · [STAGE14](./docs/STAGE14.md) · [STAGE15](./docs/STAGE15.md) · [STAGE16](./docs/STAGE16.md) · [STAGE18](./docs/STAGE18.md) · [STAGE19](./docs/STAGE19.md) · [STAGE12 gaps](./docs/STAGE12_CRITICAL_GAPS.md)

---

## Что уже работает

- HTTP API gateway: `/health`, `/api/status`, `/api/message`, `/api/inbound`, `/api/tools`, `/api/sessions`
- Realtime SSE stream: `/api/events/stream`
- Cron semantic baseline: `status/list/add/edit/enable/disable/run/runs/validate/rm` + sessionTarget/payload/delivery/wakeMode
- Baseline control-plane для gateway: `gateway(run)|status|start|stop|restart|health|probe|call`
- Session store + JSONL-транскрипты
- Scoped policy для tools (`global` / `channels` / `peers`)
- Telegram baseline + pairing policy/approve + baseline message actions (`send/react/delete/poll`)
- Baseline управления каналами: `channels list|status|capabilities|resolve|add|remove` (telegram)
- Task lane API (`/api/tasks`) с timeout/cancel baseline
- Security baseline: auth token mode, rate limit по источнику, audit JSONL
- Browser relay Stage 18 baseline: native backend (`browser.backend=native`) для `status/open/navigate/snapshot/click/type/screenshot`, с fallback/compat через `openclaw_cli`
- Admin dashboard Stage 19 slice 2: `/admin` как операторская консоль (KPI, refresh controls, сессии/cron, safe quick-actions, logs/audit tail)
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
cd ~/PycharmProjects/NexaClawProject
cp config/config.example.json config/config.json

# Нужен для ответов через LLM
export OPENAI_API_KEY="<твой_ключ>"

scripts/bootstrap.sh

# Опционально, но рекомендуется: пошаговый терминальный setup wizard (RU/EN)
./build/nexaclaw setup --config config/config.json

./build/nexaclaw run --config config/config.json
```

Проверка:

```bash
curl -s http://127.0.0.1:18890/health
```

---

## Установка одной командой

> Важно: бренд уже **NexaClaw**, но GitHub-репозиторий пока оставлен с legacy-именем `ClawForgeProject` ради совместимости миграции.

Пользовательская установка (рекомендуется):

```bash
curl -fsSL https://raw.githubusercontent.com/LITVA-HUB/ClawForgeProject/main/scripts/install.sh | bash
```

Более безопасный вариант (сначала посмотреть код):

```bash
git clone https://github.com/LITVA-HUB/ClawForgeProject.git && cd ClawForgeProject && bash scripts/install.sh
```

Системная установка:

```bash
bash scripts/install.sh --system
# обновить существующую установку
bash scripts/install.sh --update
```

Детерминированная установка с пином коммита:

```bash
bash scripts/install.sh --pin-commit <full_sha>
```

Dry-run (без изменений):

```bash
bash scripts/install.sh --dry-run
# только проверка зависимостей
bash scripts/install.sh --validate
```

Заметки по безопасности:
- Скрипт клонирует/обновляет репозиторий, собирает через CMake и ставит бинарь `nexaclaw`.
- `--update` обновляет существующий клон и переустанавливает бинарь.
- `--validate` только проверяет зависимости и не вносит изменения.
- Для воспроизводимости используй `--pin-commit`.
- `curl|bash` = доверие источнику; по возможности сначала инспектируй скрипт.

## CLI шпаргалка

```bash
./build/nexaclaw --help
./build/nexaclaw --doctor --config config/config.json
./build/nexaclaw status --config config/config.json
./build/nexaclaw cron list --config config/config.json
./build/nexaclaw tools list --config config/config.json
./build/nexaclaw pairing list --config config/config.json
./build/nexaclaw pairing approve <code> --config config/config.json
./build/nexaclaw models list --config config/config.json
./build/nexaclaw models status --config config/config.json
./build/nexaclaw models set anthropic/claude-3-5-haiku-latest --config config/config.json
./build/nexaclaw models aliases add fast anthropic/claude-3-5-haiku-latest --config config/config.json
./build/nexaclaw models fallbacks add openai/gpt-4o-mini --config config/config.json
./build/nexaclaw models auth add --provider openai --profile-id work --api-key-env OPENAI_API_KEY --config config/config.json
./build/nexaclaw models auth use --provider openai --profile-id work --config config/config.json
./build/nexaclaw models auth login --provider openai-codex --config config/config.json
./build/nexaclaw models auth order set --provider openai-codex --profile-id codex-work --profile-id codex-personal --config config/config.json
./build/nexaclaw models auth setup-token --provider openai-codex --profile-id codex --token <token> --expires-in 3600 --config config/config.json
./build/nexaclaw browser navigate https://example.com --config config/config.json
./build/nexaclaw browser click e6 --config config/config.json
./build/nexaclaw message react --channel telegram --target @chat --message-id 123 --emoji ✅ --dry-run --config config/config.json
./build/nexaclaw config get model.current --config config/config.json
./build/nexaclaw config set model.current fast --config config/config.json
```

## Multi-model маршрутизация

`modelsConfig` поддерживает `providers/models/routing`:
- baseline-провайдеры: `openai`, `anthropic`, `openrouter`, `gemini`, `minimax`
- routing: `current` + `aliases` + `fallbacks`
- если API формат провайдера несовместим, NexaClaw возвращает честную diagnostic-ошибку.

## OpenClaw compatibility

В Stage 10 NexaClaw получил практичный parity CLI для ключевых OpenClaw-сценариев.

### 1:1 или почти 1:1
- `status`, `health`, `doctor`, `sessions`
- `cron status|list|add|edit|enable|disable|run|runs|validate|rm`
- `tools list|call`
- `models list|status|set|aliases|fallbacks|probe|set-image`
- `models auth list|add|login|paste-token|setup-token|use|remove` + `models auth order get|set|clear`
- `image-fallbacks list|add|remove|clear`
- `browser status|open|navigate|snapshot|click|type|screenshot` (Stage 16 baseline через `openclaw_cli`)
- `config get/set` (расширено покрытие ключей)
- `logs tail`, `system event`, `pairing list|approve`
- `message send --channel telegram --target ... --message ...` (baseline со строгой валидацией target)
- `channels list|status|capabilities|resolve|add|remove` (telegram baseline)

### Migration guide (OpenClaw -> NexaClaw)
- `openclaw browser status` -> `nexaclaw browser status`
- `openclaw browser open <url>` -> `nexaclaw browser open <url>`
- `openclaw cron add --json ...` -> `nexaclaw cron add --json ...`
- `openclaw tools call <name> --json ...` -> `nexaclaw tools call <name> --json ...`
- `openclaw models probe` -> `nexaclaw models probe`
- `openclaw models set-image <model>` -> `nexaclaw models set-image <model>`
- `openclaw image-fallbacks ...` -> `nexaclaw image-fallbacks ...`

Полная матрица (`implemented` / `partial` / `stub` / `impossible-now`) — в `docs/CLI_PARITY.md`.

Язык CLI:

```bash
./build/nexaclaw --lang ru --help
./build/nexaclaw --lang en --help
```

---

## Быстрые примеры API

### Опциональная auth-защита

Если `gateway.auth.mode = "token"`, добавляй заголовок:

```bash
-H "Authorization: Bearer $NEXACLAW_GATEWAY_TOKEN"
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
scripts/smoke_models_cli.sh
scripts/smoke_stage11_models_auth.sh
scripts/smoke_stage11_installer.sh
scripts/smoke_stage12_gateway_security.sh
scripts/smoke_stage13_setup_wizard.sh
scripts/smoke_stage14_cron_semantics.sh
scripts/smoke_stage15_message_channels.sh
scripts/smoke_stage16_browser_oauth_message.sh
scripts/smoke_stage18_native_browser.sh
scripts/smoke_stage19_admin_dashboard.sh
scripts/smoke_stage21_agents.sh
scripts/smoke_stage22_nodes_canvas_devices.sh
scripts/smoke_stage23_control_plane_slice1.sh
scripts/smoke_stage25_nodes_runtime_slice2.sh

# строгая сборка (warning == error)
cmake -S . -B build_warnings -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build_warnings -j

# быстрый перф-чек
scripts/benchmark_quick.sh 50
```

---

## Шаблоны сервисов

- systemd: `deploy/nexaclaw.service`
- launchd: `deploy/com.nexaclaw.agent.plist`

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

- Browser parity имеет нативный baseline backend (`browser.backend=native`) для core command/API flow, включая моделирование GET form-submit side effects и structured capability gates; для реальной browser automation parity остаётся `browser.backend=openclaw_cli`
- `agent/agents` CLI пока baseline-only (list/show/create/delete/use/run + детерминированный fallback tasks/message/session), это не полная parity orchestration OpenClaw
- Telegram пока baseline, а не full multi-channel экосистема
- Canvas/Nodes/Devices получили Stage 25 slice 2 practical uplift: локальный read-safe runtime probe/invoke, virtual canvas snapshot и явные capability-gated ошибки при недоступном runtime
- Нет полной внешней orchestration-модели subagents

Актуальный статус смотри в [docs/PARITY_ROADMAP.md](./docs/PARITY_ROADMAP.md).
