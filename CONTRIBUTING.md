# Contributing to NexaClaw

Thanks for helping improve NexaClaw.

## Good first contributions

- Improve documentation and examples.
- Add C++ tests for gateway behavior.
- Improve CLI help text and diagnostics.
- Add smoke tests for stable workflows.
- Improve CI and release automation.

## Development setup

```bash
git clone https://github.com/LITVA-HUB/ClawForgeProject.git
cd ClawForgeProject
cp config/config.example.json config/config.json
scripts/bootstrap.sh
```

## Quality checks

```bash
cmake -S . -B build
cmake --build build -j

cmake -S . -B build_warnings -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build_warnings -j

scripts/smoke_full.sh
```

## Pull requests

Please keep pull requests focused and describe:

- what changed;
- why it matters;
- how it was tested;
- any compatibility or security impact.

For security-sensitive changes, mention auth, rate limiting, tool scopes, audit logging, and browser relay boundaries where relevant.
