# Stage 12 — Gateway + Security baseline

## Delivered

### Gateway CLI baseline

Implemented in `nexaclaw gateway`:

- `gateway status`
- `gateway start`
- `gateway stop`
- `gateway restart`
- `gateway health`
- `gateway call <method> --params <json>`

Current `gateway call` methods:

- `health`
- `status`
- `config.get`
- `config.apply`
- `config.patch`
- `update.run` (explicit not-implemented response)

Notes:

- process lifecycle uses local pid/log files under `state/`.
- `config.apply/config.patch` perform JSON validation and hash checks (`baseHash` when provided).

### Security audit baseline

Implemented in `nexaclaw security`:

- `security audit`
- `security audit --deep`
- `security audit --fix`

Current checks:

- DM session isolation risk (`api.dmScope`)
- gateway auth token env presence (`gateway.auth.mode=token`)
- config/state permissions sanity
- optional deep gateway probe (`/health`)

`--fix` currently applies safe baseline remediations (dmScope + permissions).

### Naming cleanup continuation

User-facing naming now consistently uses NexaClaw for:

- `/health` and `/api/status` service labels
- startup logs and startup event service id
- `/status` textual response
- Telegram pairing CLI hint path
- temp payload file naming

## Validation

Added Stage12 smoke:

- `scripts/smoke_stage12_gateway_security.sh`

Included into full smoke pipeline:

- `scripts/smoke_full.sh`

Recommended full QA run:

```bash
cmake -S . -B build
cmake --build build -j
cmake -S . -B build_warnings -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build_warnings -j
bash scripts/smoke_full.sh
```

## Remaining gap after Stage12 baseline

See: `docs/STAGE12_CRITICAL_GAPS.md`
