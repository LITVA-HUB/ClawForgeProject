# Cron Job Reference

NexaClaw cron jobs are stored in `state/cron/jobs.json` and managed via CLI or API.

## Job schema

```json
{
  "id": "daily-report",
  "enabled": true,
  "schedule": "0 9 * * *",
  "sessionTarget": "main",
  "payload": "/report daily",
  "delivery": "message",
  "wakeMode": "ensure-running",
  "description": "Send daily report at 09:00"
}
```

### Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Unique job identifier |
| `enabled` | bool | yes | Whether the job runs |
| `schedule` | string | yes | Cron expression (5-field: min hour dom mon dow) |
| `sessionTarget` | string | no | Session key to route the payload to |
| `payload` | string | no | Text or command to deliver |
| `delivery` | string | no | `message` (default) or `inbound` |
| `wakeMode` | string | no | `ensure-running` \| `skip-if-stopped` |
| `description` | string | no | Human-readable label |

## CLI commands

```bash
# List all jobs
./build/nexaclaw cron list --config config/config.json

# Show status (next run, last run, errors)
./build/nexaclaw cron status --config config/config.json

# Add a job from JSON
./build/nexaclaw cron add --json '{"id":"ping","schedule":"* * * * *","payload":"/status"}' --config config/config.json

# Edit a field
./build/nexaclaw cron edit ping --field enabled --value false --config config/config.json

# Enable / disable
./build/nexaclaw cron enable ping --config config/config.json
./build/nexaclaw cron disable ping --config config/config.json

# Trigger manually
./build/nexaclaw cron run ping --config config/config.json

# Show run history
./build/nexaclaw cron runs ping --config config/config.json

# Validate schedule expression
./build/nexaclaw cron validate "0 9 * * 1-5" --config config/config.json

# Remove a job
./build/nexaclaw cron rm ping --config config/config.json
```

## Cron expression format

Standard 5-field cron. Examples:

| Expression | Meaning |
|-----------|---------|
| `* * * * *` | Every minute |
| `0 * * * *` | Every hour at :00 |
| `0 9 * * *` | Daily at 09:00 |
| `0 9 * * 1-5` | Weekdays at 09:00 |
| `*/15 * * * *` | Every 15 minutes |
| `0 0 1 * *` | First day of each month |

## Run history

Run history is stored in `state/cron/runs/<job-id>.jsonl`. Each line:

```json
{"id":"daily-report","triggeredAt":"2026-06-04T09:00:01Z","result":"ok","durationMs":42}
```
