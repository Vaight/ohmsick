#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"

BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-macos}"
CONFIG="${CONFIG:-Release}"
TARGET="${TARGET:-ohmsick_macos}"
GENERATOR="${GENERATOR:-Unix Makefiles}"
MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
BUILD_JOBS="${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"

if [[ -z "${CMAKE_OSX_ARCHITECTURES:-}" ]]; then
    case "$(uname -m)" in
        arm64)  CMAKE_OSX_ARCHITECTURES="arm64" ;;
        x86_64) CMAKE_OSX_ARCHITECTURES="x86_64" ;;
        *)
            print -u2 "Unsupported macOS architecture: $(uname -m)"
            exit 1
            ;;
    esac
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DCMAKE_OSX_ARCHITECTURES="$CMAKE_OSX_ARCHITECTURES" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "$BUILD_DIR" \
    --config "$CONFIG" \
    --target "$TARGET" \
    --parallel "$BUILD_JOBS"

VST3_PATH="$BUILD_DIR/ohmsick_plugin_artefacts/$CONFIG/VST3/Ohmsick.vst3"
DEBUG_HOST_PATH="$BUILD_DIR/debug_host_artefacts/$CONFIG/Ohmsick Debug Host.app"

if [[ -d "$VST3_PATH" ]]; then
    print "Built VST3: $VST3_PATH"
fi

if [[ -d "$DEBUG_HOST_PATH" ]]; then
    print "Built debug host: $DEBUG_HOST_PATH"
fi
