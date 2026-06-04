# Installing NexaClaw

## Quick install (recommended)

```bash
git clone https://github.com/LITVA-HUB/ClawForgeProject.git
cd ClawForgeProject
bash scripts/install.sh
```

The install script builds the binary and places it at `~/.local/bin/nexaclaw` (user install) or `/usr/local/bin/nexaclaw` (system install with `--system`).

## Requirements

| Requirement | Minimum version |
|-------------|----------------|
| C++ compiler | clang++ 12 / g++ 10 / AppleClang 13 |
| CMake | 3.20 |
| curl | any |
| python3 | 3.8 (for smoke scripts) |
| bash | 4.0 |

macOS ships with an older bash. Install a newer one via Homebrew: `brew install bash`.

## Manual build

```bash
git clone https://github.com/LITVA-HUB/ClawForgeProject.git
cd ClawForgeProject

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Verify
./build/nexaclaw --version
```

## Install options

```bash
# User install (~/.local/bin/nexaclaw)
bash scripts/install.sh

# System install (/usr/local/bin/nexaclaw)
bash scripts/install.sh --system

# Update existing install
bash scripts/install.sh --update

# Pin to a specific commit (reproducible)
bash scripts/install.sh --pin-commit <full_sha>

# Dry run — no changes
bash scripts/install.sh --dry-run

# Check prerequisites only
bash scripts/install.sh --validate
```

## First run

```bash
cp config/config.example.json config/config.json
$EDITOR config/config.json          # set your API keys via apiKeyEnv references
export OPENAI_API_KEY="sk-..."      # or whichever provider you use

./build/nexaclaw run --config config/config.json
```

Check it's running:

```bash
curl -s http://127.0.0.1:18890/health
# {"status":"ok"}
```

## Uninstall

```bash
rm ~/.local/bin/nexaclaw
# or
rm /usr/local/bin/nexaclaw
```

State and config are in the clone directory — remove the clone to clean everything up.
