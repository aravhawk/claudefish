#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

# 1. Install Emscripten SDK if not present
if [ ! -d "emsdk" ]; then
  echo ">>> Cloning Emscripten SDK..."
  git clone https://github.com/emscripten-core/emsdk.git
fi

# 2. Install and activate latest emsdk if not already done
if [ ! -f "emsdk/.emsdk_version" ] || ! emsdk/upstream/emscripten/emcc --version &>/dev/null; then
  echo ">>> Installing and activating latest Emscripten..."
  cd emsdk
  ./emsdk install latest
  ./emsdk activate latest
  cd "$PROJECT_DIR"
else
  echo ">>> Emscripten SDK already installed and activated."
fi

# 3. Run pnpm install if node_modules doesn't exist or package.json is newer
if [ ! -d "node_modules" ] || [ "package.json" -nt "node_modules/.package-lock.json" ] 2>/dev/null; then
  echo ">>> Installing Node.js dependencies..."
  pnpm install
else
  echo ">>> Node.js dependencies already up to date."
fi

# 4. Create public/engine/ directory if it doesn't exist
if [ ! -d "public/engine" ]; then
  echo ">>> Creating public/engine/ directory..."
  mkdir -p public/engine
else
  echo ">>> public/engine/ directory already exists."
fi

echo ">>> Init complete."
