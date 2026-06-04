#!/usr/bin/env bash
# Check that required environment variables are set before starting NexaClaw.
# Usage: source scripts/check_env.sh   or   scripts/check_env.sh [provider]
#
# Provider options: openai, anthropic, openrouter, gemini, minimax, telegram, all

set -euo pipefail
PROVIDER="${1:-openai}"
MISSING=0

require_env() {
  local var="$1" desc="$2"
  if [ -z "${!var:-}" ]; then
    echo "  MISSING  $var  ($desc)"
    MISSING=$((MISSING+1))
  else
    echo "  OK       $var"
  fi
}

echo "=== NexaClaw env check (provider: $PROVIDER) ==="
echo ""

case "$PROVIDER" in
  openai|all)
    require_env OPENAI_API_KEY "OpenAI API key" ;;
esac

case "$PROVIDER" in
  anthropic|all)
    require_env ANTHROPIC_API_KEY "Anthropic API key" ;;
esac

case "$PROVIDER" in
  openrouter|all)
    require_env OPENROUTER_API_KEY "OpenRouter API key" ;;
esac

case "$PROVIDER" in
  gemini|all)
    require_env GEMINI_API_KEY "Gemini API key" ;;
esac

case "$PROVIDER" in
  minimax|all)
    require_env MINIMAX_API_KEY "MiniMax API key" ;;
esac

case "$PROVIDER" in
  telegram|all)
    require_env TELEGRAM_BOT_TOKEN "Telegram bot token" ;;
esac

echo ""
if [ "$MISSING" -eq 0 ]; then
  echo "All required env vars are set."
else
  echo "$MISSING required env var(s) are missing. Set them before starting NexaClaw."
  exit 1
fi
