#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-linux}"
CONFIG="${CONFIG:-Release}"
TARGET="${TARGET:-}"
GENERATOR="${GENERATOR:-Unix Makefiles}"

: "${CC:=gcc}"
: "${CXX:=g++}"
export CC CXX

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build_args=(--build "$BUILD_DIR" --config "$CONFIG")
if [[ -n "$TARGET" ]]; then
    build_args+=(--target "$TARGET")
fi

cmake "${build_args[@]}"

VST3_PATH="$BUILD_DIR/vst3arduinothing_vst3_artefacts/$CONFIG/VST3/VST3 Arduino Thing.vst3"
if [[ -d "$VST3_PATH" ]]; then
    printf 'Built VST3: %s\n' "$VST3_PATH"
fi
