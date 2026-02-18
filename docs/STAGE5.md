# Stage 5 — Realtime + UX Parity

## Что добавлено

1. **SSE realtime stream**
   - Новый endpoint: `GET /api/events/stream`
   - Формат: Server-Sent Events (`text/event-stream`)
   - Публикуемые события:
     - `startup`
     - `inbound_message`
     - `assistant_reply`
     - `tool_call_result`
     - `cron_fired`
     - `error`

2. **Внутренний thread-safe EventBus**
   - Новый модуль: `core/EventBus`
   - Поддерживает подписку + ожидание событий с таймаутом
   - Встроенный ring-buffer последних событий

3. **Публикация событий из модулей**
   - `Application` → `startup`, `error`
   - `AgentEngine` → `inbound_message`, `assistant_reply`, `tool_call_result`, `error`
   - `HttpServer` → API-level `inbound_message`, `assistant_reply`, `tool_call_result`, `error`
   - `CronScheduler` → `cron_fired`, `error`
   - `TelegramBot` → `inbound_message`, `assistant_reply`, `error`

4. **Новые CLI команды**
   - `nexaclaw status`
   - `nexaclaw cron list`
   - `nexaclaw tools list`

   Поведение:
   - сначала пробует HTTP API (с авторизацией по config/env)
   - если сервис не запущен — использует локальный fallback (чтение state/config)

5. **Smoke script Stage 5**
   - `scripts/smoke_stage5.sh` (auth-aware)
   - Проверки:
     - `/health`
     - `/api/status` (401/200 в зависимости от auth mode)
     - `/api/status` с авторизацией (200)
     - SSE подключение
     - `cron run-now`

## Примеры

```bash
# SSE stream
curl -N http://127.0.0.1:18890/api/events/stream \
  -H "Authorization: Bearer $CLAWFORGE_GATEWAY_TOKEN"

# New CLI
./build/nexaclaw status
./build/nexaclaw cron list
./build/nexaclaw tools list
```

## Совместимость

Stage 1–4 поведение сохранено:
- `/health`, `/api/*`, cron, sessions, tools, telegram, pairing, auth
- RU/EN CLI UX не сломан
