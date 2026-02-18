# Stage 13 — Terminal onboarding UX (RU/EN)

## Goal

Make installation/setup feel closer to OpenClaw onboarding UX: clear terminal entrypoint, bilingual guidance, and safer defaults.

## Delivered

### New CLI onboarding surface

Implemented commands (all mapped to one wizard baseline):

- `nexaclaw setup`
- `nexaclaw onboard`
- `nexaclaw configure`

### Interactive terminal menu (RU/EN)

Wizard features:

- bilingual prompts (Russian/English)
- menu-driven setup flow
- live config summary while editing
- save + exit
- save + run doctor
- save + start gateway

### Non-interactive setup

For automation/scripts:

- `--non-interactive`
- `--yes` (alias behavior)

Applies recommended defaults and writes config safely.

### Safe defaults preset

Wizard can apply recommended baseline:

- `name = nexaclaw`
- `gateway.auth.tokenEnv = NEXACLAW_GATEWAY_TOKEN`
- `api.dmScope = per-channel-peer`
- `telegram.dmPolicy = pairing`

### Config bootstrap behavior

If config file is missing, wizard creates it from `config/config.example.json` automatically.

## Usage

```bash
./build/nexaclaw setup --config config/config.json
./build/nexaclaw onboard --config config/config.json
./build/nexaclaw configure --config config/config.json

# scripted mode
./build/nexaclaw setup --non-interactive --config config/config.json
```

## Notes

This is a practical Stage 13 baseline (not full OpenClaw onboarding parity yet):

- no TUI stack
- no multi-provider login dance in wizard yet
- no deep channel wizard ecosystem yet

It is intentionally lightweight, predictable, and scriptable.
