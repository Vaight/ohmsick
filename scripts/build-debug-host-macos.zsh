#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"

TARGET=debug_host "$SCRIPT_DIR/build-macos.zsh"
