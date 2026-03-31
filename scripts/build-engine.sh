#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ENGINE_DIR="$PROJECT_DIR/engine/src"
OUTPUT_DIR="$PROJECT_DIR/public/engine"

if ! command -v emcc >/dev/null 2>&1; then
  echo "error: emcc is not available. Run 'source ./emsdk/emsdk_env.sh' first." >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"

emcc "$ENGINE_DIR"/*.c \
  -I"$ENGINE_DIR" \
  -O3 \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME=createChessEngine \
  -s ASSERTIONS=1 \
  -s EXPORTED_FUNCTIONS='["_init_engine","_set_position","_search_best_move","_evaluate_position","_get_legal_moves"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
  -s ENVIRONMENT=web,worker \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=16777216 \
  -s MAXIMUM_MEMORY=268435456 \
  -s NO_EXIT_RUNTIME=1 \
  -s FILESYSTEM=0 \
  --no-entry \
  -o "$OUTPUT_DIR/engine.js"

ls -lh "$OUTPUT_DIR/engine.js" "$OUTPUT_DIR/engine.wasm"
