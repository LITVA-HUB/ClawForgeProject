#!/usr/bin/env bash
set -euo pipefail

REPO_URL="https://github.com/LITVA-HUB/ClawForgeProject.git"
BRANCH="main"
INSTALL_DIR="${HOME}/.local/share/clawforge"
BIN_DIR="${HOME}/.local/bin"
SYSTEM=0
DRY_RUN=0
VALIDATE_ONLY=0
UPDATE_ONLY=0
PIN_COMMIT=""

usage() {
  cat <<'EOF'
ClawForge one-command installer

Usage:
  scripts/install.sh [options]

Options:
  --repo <url>         Git repository URL (default: official GitHub repo)
  --branch <name>      Git branch/tag to install (default: main)
  --pin-commit <sha>   Optional commit pin; verifies checkout exactly to SHA
  --dir <path>         Install/update source dir (default: ~/.local/share/clawforge)
  --bin-dir <path>     User binary dir (default: ~/.local/bin)
  --system             Install binary to /usr/local/bin (requires sudo)
  --update             Update existing clone only (fails if not installed yet)
  --validate           Validate local environment (git/cmake/compiler/bash); no changes
  --dry-run            Print planned actions without changing system
  -h, --help           Show this help

What script does:
  1) clone repo or fetch/update existing clone
  2) checkout branch (or pin commit if provided)
  3) configure and build with CMake
  4) copy built binary to bin dir (user or system)

Security notes:
  - Prefer --pin-commit for reproducible installs
  - Review script before running (curl | bash implies trust)
  - For max safety use: git clone + inspect + run locally
EOF
}

log() { echo "[install] $*"; }
run() {
  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "[dry-run] $*"
  else
    eval "$*"
  fi
}

need_cmd() {
  local c="$1"
  if ! command -v "$c" >/dev/null 2>&1; then
    echo "Missing required command: $c" >&2
    return 1
  fi
  return 0
}

validate_env() {
  local ok=0
  need_cmd bash || ok=1
  need_cmd git || ok=1
  need_cmd cmake || ok=1
  if command -v c++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1; then
    :
  else
    echo "Missing C++ compiler (c++/clang++/g++)" >&2
    ok=1
  fi
  if [[ "$ok" -eq 0 ]]; then
    log "Validation OK"
  else
    log "Validation FAILED"
  fi
  return "$ok"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) REPO_URL="$2"; shift 2 ;;
    --branch) BRANCH="$2"; shift 2 ;;
    --pin-commit) PIN_COMMIT="$2"; shift 2 ;;
    --dir) INSTALL_DIR="$2"; shift 2 ;;
    --bin-dir) BIN_DIR="$2"; shift 2 ;;
    --system) SYSTEM=1; shift ;;
    --update) UPDATE_ONLY=1; shift ;;
    --validate) VALIDATE_ONLY=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1"; usage; exit 1 ;;
  esac
done

if [[ "$SYSTEM" -eq 1 ]]; then
  BIN_DIR="/usr/local/bin"
fi

log "repo=$REPO_URL"
log "branch=$BRANCH"
[[ -n "$PIN_COMMIT" ]] && log "pin_commit=$PIN_COMMIT"
log "install_dir=$INSTALL_DIR"
log "bin_dir=$BIN_DIR"

validate_env

if [[ "$VALIDATE_ONLY" -eq 1 ]]; then
  log "Validate-only mode: no changes applied"
  exit 0
fi

if [[ "$UPDATE_ONLY" -eq 1 && ! -d "$INSTALL_DIR/.git" ]]; then
  echo "--update requested but install dir is not a git clone: $INSTALL_DIR" >&2
  exit 1
fi

if [[ ! -d "$INSTALL_DIR/.git" ]]; then
  run "mkdir -p $(printf %q "$(dirname "$INSTALL_DIR")")"
  run "git clone --branch $(printf %q "$BRANCH") --depth 1 $(printf %q "$REPO_URL") $(printf %q "$INSTALL_DIR")"
else
  run "git -C $(printf %q "$INSTALL_DIR") remote set-url origin $(printf %q "$REPO_URL")"
  run "git -C $(printf %q "$INSTALL_DIR") fetch origin $(printf %q "$BRANCH") --tags"
  run "git -C $(printf %q "$INSTALL_DIR") checkout $(printf %q "$BRANCH")"
  run "git -C $(printf %q "$INSTALL_DIR") reset --hard origin/$(printf %q "$BRANCH")"
fi

if [[ -n "$PIN_COMMIT" ]]; then
  run "git -C $(printf %q "$INSTALL_DIR") fetch origin $(printf %q "$PIN_COMMIT")"
  run "git -C $(printf %q "$INSTALL_DIR") checkout --detach $(printf %q "$PIN_COMMIT")"
  if [[ "$DRY_RUN" -ne 1 ]]; then
    GOT_SHA="$(git -C "$INSTALL_DIR" rev-parse HEAD)"
    if [[ "$GOT_SHA" != "$PIN_COMMIT" ]]; then
      echo "Pinned commit mismatch: expected $PIN_COMMIT got $GOT_SHA" >&2
      exit 1
    fi
  fi
fi

run "cmake -S $(printf %q "$INSTALL_DIR") -B $(printf %q "$INSTALL_DIR/build")"
run "cmake --build $(printf %q "$INSTALL_DIR/build") -j"

if [[ "$SYSTEM" -eq 1 ]]; then
  run "sudo install -m 0755 $(printf %q "$INSTALL_DIR/build/clawforge") /usr/local/bin/clawforge"
else
  run "mkdir -p $(printf %q "$BIN_DIR")"
  run "install -m 0755 $(printf %q "$INSTALL_DIR/build/clawforge") $(printf %q "$BIN_DIR/clawforge")"
fi

log "Done. Binary: $BIN_DIR/clawforge"
if [[ "$SYSTEM" -eq 0 ]]; then
  log "Ensure $BIN_DIR is in PATH"
fi
