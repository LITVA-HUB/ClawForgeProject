# Environment Variables

NexaClaw uses environment variables for secrets. Never put secret values directly in `config/config.json`.

## Gateway

| Variable | Config field | Required | Description |
|----------|-------------|----------|-------------|
| `NEXACLAW_GATEWAY_TOKEN` | `gateway.auth.tokenEnv` | When `auth.mode = "token"` | Bearer token for API authentication. Generate with `openssl rand -hex 32`. |

## Model providers

| Variable | Provider | Config field | Description |
|----------|----------|-------------|-------------|
| `OPENAI_API_KEY` | `openai` | `modelsConfig.providers.openai.apiKeyEnv` | OpenAI API key |
| `ANTHROPIC_API_KEY` | `anthropic` | `modelsConfig.providers.anthropic.apiKeyEnv` | Anthropic API key |
| `OPENROUTER_API_KEY` | `openrouter` | `modelsConfig.providers.openrouter.apiKeyEnv` | OpenRouter API key |
| `GEMINI_API_KEY` | `gemini` | `modelsConfig.providers.gemini.apiKeyEnv` | Gemini API key |
| `MINIMAX_API_KEY` | `minimax` | `modelsConfig.providers.minimax.apiKeyEnv` | MiniMax API key |

The `apiKeyEnv` field in `modelsConfig.providers` is the **name** of the environment variable, not the value. You can override it to use a different variable name:

```json
"providers": {
  "openai": {
    "apiKeyEnv": "MY_OPENAI_KEY"
  }
}
```

## Telegram

| Variable | Config field | Required | Description |
|----------|-------------|----------|-------------|
| `TELEGRAM_BOT_TOKEN` | `telegram.botTokenEnv` | When `telegram.enabled = true` | Bot token from @BotFather |

## Checking env vars

Use `--doctor` to verify that required env vars are set without exposing their values:

```bash
./build/nexaclaw --doctor --config config/config.json
```

## Setting env vars securely

For local development, use a `.env` file (excluded from git by `.gitignore`):

```bash
# .env — never commit this file
export OPENAI_API_KEY="sk-..."
export NEXACLAW_GATEWAY_TOKEN="..."
```

Source it before running:

```bash
source .env && ./build/nexaclaw run --config config/config.json
```

For systemd, use `EnvironmentFile` in the unit file:

```ini
[Service]
EnvironmentFile=/etc/nexaclaw/env
```
