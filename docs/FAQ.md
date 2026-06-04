# Frequently Asked Questions

## General

**Q: What is NexaClaw?**  
A self-hosted HTTP gateway and local control plane for AI agent workflows. It runs on your machine, keeps your data local, and gives you a CLI and API to manage sessions, tools, cron jobs, and model routing.

**Q: Why C++?**  
Low overhead, no runtime dependencies beyond the OS, and fast startup. The gateway binary is self-contained and easy to deploy as a systemd or launchd service.

**Q: What models does it support?**  
Any OpenAI-compatible endpoint, plus native support for Anthropic, Gemini, and MiniMax API styles. See `modelsConfig.providers` in `config/config.example.json`.

---

## Setup

**Q: Do I need an OpenAI key?**  
Only if you use an OpenAI model. NexaClaw supports Anthropic, OpenRouter, Gemini, and MiniMax too. You can run without any LLM key if you only use non-LLM tools and cron.

**Q: Can I run it without Telegram?**  
Yes. Set `telegram.enabled: false` (the default). Telegram is optional.

**Q: How do I update NexaClaw?**  
```bash
bash scripts/install.sh --update
```
Or manually: `git pull && cmake --build build -j`

---

## Security

**Q: Is it safe to expose NexaClaw to the network?**  
Only behind a reverse proxy with TLS, and only with `gateway.auth.mode: "token"`. By default it binds to `127.0.0.1` (loopback only). See [docs/SELF_HOSTING.md](./SELF_HOSTING.md).

**Q: Where are API keys stored?**  
They are never stored. NexaClaw reads them from environment variables at startup. The `apiKeyEnv` config field names the variable, not its value. See [docs/ENV_VARS.md](./ENV_VARS.md).

**Q: What does the audit log contain?**  
Timestamped JSONL records of sensitive gateway actions: auth events, tool calls, rate-limit hits, Telegram pairings, and task lifecycle events. It does not log message content by default.

---

## Troubleshooting

**Q: The gateway starts but LLM responses don't work.**  
Check `echo $OPENAI_API_KEY` (or whichever provider). Run `./build/nexaclaw models probe --config config/config.json`.

**Q: Cron jobs don't run.**  
Check `cron.enabled: true` and that the job has `enabled: true`. Run `./build/nexaclaw cron status`.

**Q: I get 429 on every request.**  
Raise `rateLimit.maxRequests` or lower `rateLimit.windowMs` in config.

For more, see [docs/TROUBLESHOOTING.md](./TROUBLESHOOTING.md).

---

## Contributing

**Q: How do I contribute?**  
See [CONTRIBUTING.md](../CONTRIBUTING.md). Good first tasks: tests, docs, CLI diagnostics, smoke scripts.

**Q: How do I report a security issue?**  
See [SECURITY.md](../SECURITY.md). Do not open public issues for vulnerabilities.
