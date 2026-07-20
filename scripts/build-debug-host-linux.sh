#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

TARGET=debug_host "$SCRIPT_DIR/build-linux.sh"

BUILD_DIR="${BUILD_DIR:-$(cd -- "$SCRIPT_DIR/.." && pwd)/build-linux}"
CONFIG="${CONFIG:-Release}"
DEBUG_HOST_PATH="$BUILD_DIR/debug_host_artefacts/$CONFIG/Ohmsick Debug Host"

if [[ -x "$DEBUG_HOST_PATH" ]]; then
    printf 'Built debug host: %s\n' "$DEBUG_HOST_PATH"
fi
