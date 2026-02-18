# STAGE11 — Models Auth Parity Baseline

## Goal
Close the parity gap for model authentication beyond plain environment API keys.

## Delivered

1. **Auth profile subsystem**
   - Local store: `state/models/auth-profiles.json`
   - Profile schema: `id`, `provider`, `mode` (`api_key|oauth_token`), `token`, `expiresAt`, `meta`
   - Active profile map per provider is persisted in same store.

2. **CLI commands**
   - `models auth list`
   - `models auth add --provider --profile-id --api-key-env|--token-env`
   - `models auth paste-token --provider --profile-id --token --expires-in`
   - `models auth setup-token --provider openai-codex [--profile-id] [--token] [--expires-in]`
   - `models auth use --provider --profile-id`
   - `models auth remove --profile-id`

3. **OpenAI Codex OAuth compatibility baseline**
   - `setup-token` supports non-interactive (`--token`) and prompt input.
   - Honest baseline behavior: no fake device-code flow; explicit warning is returned.

4. **Routing preference**
   - LLM client now prefers active auth profile token over provider env key.
   - On expired profile token, resolver warns and falls back to env key if present.

5. **Diagnostics upgrade**
   - `models status` and `models probe` now include:
     - `authSource` (`profile|env|missing`)
     - `authMode`
     - `profileId`
     - `expiresAt`
     - `expired`
     - `warnings`

6. **QA / smoke**
   - Added `scripts/smoke_stage11_models_auth.sh`
   - Added `scripts/smoke_stage11_installer.sh` (installer dry-run)
   - Included both into `scripts/smoke_full.sh`

7. **One-command installer**
   - Added `scripts/install.sh` for clone/update + build + install.
   - Targets user install (`~/.local/bin`) by default and system install via `--system`.
   - Added safety controls: `--dry-run`, `--pin-commit`, explicit script behavior docs.

## Known limits (explicit)
- No real OAuth device-code flow yet.
- No encrypted token-at-rest yet (JSON file with masked output in CLI list).

## Next step
- Implement device-code OAuth flow for `openai-codex`.
- Add optional secure OS-keychain storage backend.
