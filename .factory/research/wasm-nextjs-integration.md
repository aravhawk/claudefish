# WASM + Next.js Integration Research Report

> **Date:** 2026-03-30
> **Context:** Chess engine in C → Emscripten → WASM → Next.js (App Router, Turbopack) with Web Workers

---

## Table of Contents

1. [Emscripten Compilation Flags](#1-emscripten-compilation-flags)
2. [Exposing a C API to JavaScript](#2-exposing-a-c-api-to-javascript)
3. [Loading WASM in Next.js App Router](#3-loading-wasm-in-nextjs-app-router)
4. [Web Worker Integration with Next.js](#4-web-worker-integration-with-nextjs)
5. [Comlink vs Raw postMessage](#5-comlink-vs-raw-postmessage)
6. [Turbopack + WASM Gotchas](#6-turbopack--wasm-gotchas)
7. [chess.js Library Status](#7-chessjs-library-status)
8. [Recommended Architecture](#8-recommended-architecture)
9. [Complete Code Patterns](#9-complete-code-patterns)

---

## 1. Emscripten Compilation Flags

### Recommended Build Command

```bash
emcc engine/engine.c \
  -o public/engine/engine.js \
  -O3 \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="createChessEngine" \
  -s EXPORTED_FUNCTIONS='["_search_best_move","_evaluate_position","_init_engine","_set_position","_make_move","_unmake_move","_get_best_move_uci"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","allocateUTF8"]' \
  -s ENVIRONMENT='web,worker' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=16777216 \
  -s MAXIMUM_MEMORY=268435456 \
  -s NO_EXIT_RUNTIME=1 \
  -s FILESYSTEM=0 \
  -s SINGLE_FILE=0 \
  --no-entry
```

### Flag Breakdown

| Flag | Purpose |
|------|---------|
| `-O3` | Maximum optimization for production (use `-O0` or `-O1` for development/debugging) |
| `-s WASM=1` | Explicitly emit WASM (default since Emscripten 2.x, but explicit is clearer) |
| `-s MODULARIZE=1` | Wraps output in a module factory function — **critical** for use in bundlers and workers |
| `-s EXPORT_NAME="createChessEngine"` | Names the module factory function (default is `Module`) |
| `-s EXPORTED_FUNCTIONS='[...]'` | C functions to keep alive (must prefix with `_`). Without this, dead code elimination removes them |
| `-s EXPORTED_RUNTIME_METHODS='[...]'` | JS runtime helpers to export. `ccall`/`cwrap` are the primary ones for calling C functions |
| `-s ENVIRONMENT='web,worker'` | Target both browser main thread and Web Worker contexts. Excludes Node.js/shell overhead |
| `-s ALLOW_MEMORY_GROWTH=1` | Let WASM heap grow dynamically (important for variable-size search trees) |
| `-s INITIAL_MEMORY=16777216` | 16 MB initial heap |
| `-s MAXIMUM_MEMORY=268435456` | 256 MB max heap |
| `-s NO_EXIT_RUNTIME=1` | Keep runtime alive after `main()` returns (needed for ongoing function calls) |
| `-s FILESYSTEM=0` | Disable virtual filesystem (not needed for a chess engine, saves ~50KB) |
| `-s SINGLE_FILE=0` | Keep `.wasm` as a separate file (better caching, streaming compilation) |
| `--no-entry` | No `main()` function required — we're building a library |

### Why MODULARIZE Matters

Without `-s MODULARIZE=1`, Emscripten outputs a script that immediately creates a global `Module` object. This causes problems in bundlers (webpack/Turbopack) and Web Workers. With `MODULARIZE`, the output is a function you call to instantiate the module:

```javascript
// The generated engine.js exports a factory function:
const Module = await createChessEngine({
  // Optional overrides:
  locateFile: (path) => `/engine/${path}`, // tells it where to find the .wasm file
});
```

### Development vs Production

```bash
# Development build (with debug info and assertions)
emcc engine.c -o engine.js \
  -O0 -g \
  -s ASSERTIONS=2 \
  -s SAFE_HEAP=1 \
  -s MODULARIZE=1 \
  ...

# Production build
emcc engine.c -o engine.js \
  -O3 \
  -s ASSERTIONS=0 \
  -s MODULARIZE=1 \
  ...
```

---

## 2. Exposing a C API to JavaScript

### C Side: The Engine API

```c
// engine.h
#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H

#include <emscripten.h>

#ifdef __cplusplus
extern "C" {
#endif

// Use EMSCRIPTEN_KEEPALIVE as an alternative to EXPORTED_FUNCTIONS
// It prevents dead code elimination for the decorated function

EMSCRIPTEN_KEEPALIVE
void init_engine(void);

EMSCRIPTEN_KEEPALIVE
int set_position(const char* fen);

EMSCRIPTEN_KEEPALIVE
int make_move(const char* move_uci);

EMSCRIPTEN_KEEPALIVE
int evaluate_position(void);

EMSCRIPTEN_KEEPALIVE
const char* search_best_move(int depth, int time_ms);

EMSCRIPTEN_KEEPALIVE
const char* get_legal_moves(void);

#ifdef __cplusplus
}
#endif

#endif
```

### JavaScript Side: Calling C Functions

There are two primary approaches:

#### Approach A: `cwrap` (Recommended for Repeated Calls)

```typescript
// types/engine.ts
export interface ChessEngineAPI {
  initEngine: () => void;
  setPosition: (fen: string) => number;
  makeMove: (moveUci: string) => number;
  evaluatePosition: () => number;
  searchBestMove: (depth: number, timeMs: number) => string;
  getLegalMoves: () => string;
}

// worker/engine-wrapper.ts
async function createAPI(Module: any): Promise<ChessEngineAPI> {
  return {
    initEngine: Module.cwrap('init_engine', null, []),
    setPosition: Module.cwrap('set_position', 'number', ['string']),
    makeMove: Module.cwrap('make_move', 'number', ['string']),
    evaluatePosition: Module.cwrap('evaluate_position', 'number', []),
    searchBestMove: Module.cwrap('search_best_move', 'string', ['number', 'number']),
    getLegalMoves: Module.cwrap('get_legal_moves', 'string', []),
  };
}
```

#### Approach B: `ccall` (For One-Off Calls)

```typescript
const result = Module.ccall(
  'search_best_move',  // C function name
  'string',            // return type
  ['number', 'number'], // argument types
  [6, 5000]            // arguments: depth=6, time=5000ms
);
```

### Type Mapping Reference

| C Type | cwrap/ccall Type | Notes |
|--------|-----------------|-------|
| `int`, `float`, `double` | `'number'` | All numeric types map to `'number'` |
| `void` | `null` | For return type only |
| `char*` (string) | `'string'` | Auto-converts JS string ↔ C string |
| `char*` (buffer) | `'number'` | Pass pointer, manage memory manually |
| `bool` | `'boolean'` | Maps to JS boolean |

### EMSCRIPTEN_KEEPALIVE vs EXPORTED_FUNCTIONS

- **`EMSCRIPTEN_KEEPALIVE`**: Attribute on function declaration. Simpler, keeps function through optimization.
- **`-s EXPORTED_FUNCTIONS`**: Compile-time flag listing all exports. Must prefix with `_`. More explicit control.
- **Recommendation**: Use both. `EMSCRIPTEN_KEEPALIVE` in C code for self-documentation, and also list in `EXPORTED_FUNCTIONS` for the build script as a safety net.

---

## 3. Loading WASM in Next.js App Router

### Strategy: Place WASM in `public/` and Fetch Manually

**This is the most reliable approach** for Next.js with Turbopack. Rather than trying to make webpack/Turbopack import `.wasm` files (which has many edge cases), place the compiled output in `public/` and load it at runtime.

#### File Structure

```
public/
  engine/
    engine.js          # Emscripten JS glue code
    engine.wasm        # Compiled WASM binary
src/
  workers/
    chess-engine.worker.ts   # Web Worker that loads the engine
  lib/
    engine-client.ts         # Main-thread API to communicate with worker
  hooks/
    useChessEngine.ts        # React hook for components
```

#### Why `public/` Instead of Webpack WASM Import?

1. **Turbopack compatibility**: Turbopack's WASM support is still evolving. Direct `.wasm` imports may not work correctly (see [Section 6](#6-turbopack--wasm-gotchas)).
2. **Emscripten's glue code**: Emscripten generates a `.js` file alongside the `.wasm`. The JS glue handles initialization, memory setup, and provides `ccall`/`cwrap`. This is **not** a standard WASM module that webpack can tree-shake.
3. **Web Worker loading**: Workers need a URL to `importScripts()` or `import()` the glue code. Having it in `public/` gives a reliable URL path.
4. **Caching**: `.wasm` files in `public/` get served with proper caching headers. Browsers can use streaming compilation (`WebAssembly.compileStreaming`).

#### Alternative: Webpack asyncWebAssembly (For Non-Emscripten WASM)

If you had a standalone `.wasm` file (not from Emscripten), you could use webpack's experimental WASM support:

```javascript
// next.config.mjs — only works with webpack, NOT Turbopack
/** @type {import('next').NextConfig} */
const nextConfig = {
  webpack: (config, { isServer }) => {
    config.experiments = {
      ...config.experiments,
      asyncWebAssembly: true,
    };
    // Fix for WASM in client-side bundles
    if (!isServer) {
      config.output.webassemblyModuleFilename = 'static/wasm/[modulehash].wasm';
    }
    return config;
  },
};
export default nextConfig;
```

**⚠️ This does NOT work with Turbopack.** And it's not ideal for Emscripten modules which have their own loading mechanism.

---

## 4. Web Worker Integration with Next.js

### Pattern: Web Worker with `new URL()` Syntax

Next.js (both webpack and Turbopack) supports creating Web Workers using the `new URL()` + `new Worker()` pattern:

```typescript
// This pattern is recognized by both webpack and Turbopack
const worker = new Worker(
  new URL('../workers/chess-engine.worker.ts', import.meta.url),
  { type: 'module' }
);
```

### Critical: Workers Must Be Created in `useEffect`

Since Next.js uses SSR, `Worker` is not available on the server. Always initialize workers in `useEffect`:

```typescript
'use client';

import { useEffect, useRef } from 'react';

export function useChessEngine() {
  const workerRef = useRef<Worker | null>(null);

  useEffect(() => {
    // Worker is only available in the browser
    workerRef.current = new Worker(
      new URL('../workers/chess-engine.worker.ts', import.meta.url),
      { type: 'module' }
    );

    return () => {
      workerRef.current?.terminate();
      workerRef.current = null;
    };
  }, []);

  // ... rest of hook
}
```

### Worker File Structure

```typescript
// workers/chess-engine.worker.ts

// Import Comlink if using it, or set up postMessage handlers
import * as Comlink from 'comlink';

// The WASM module will be loaded dynamically
let engineAPI: any = null;

async function initEngine() {
  // Import the Emscripten glue code from public/
  // Using importScripts for classic workers, or dynamic import for module workers
  const createModule = (await import(/* webpackIgnore: true */ '/engine/engine.js')).default;

  const Module = await createModule({
    locateFile: (path: string) => `/engine/${path}`,
  });

  engineAPI = {
    initEngine: Module.cwrap('init_engine', null, []),
    setPosition: Module.cwrap('set_position', 'number', ['string']),
    searchBestMove: Module.cwrap('search_best_move', 'string', ['number', 'number']),
    evaluatePosition: Module.cwrap('evaluate_position', 'number', []),
    getLegalMoves: Module.cwrap('get_legal_moves', 'string', []),
  };

  engineAPI.initEngine();
}

// ... expose via Comlink or postMessage
```

### Loading Emscripten JS Glue Inside a Worker

There are several approaches to load the Emscripten-generated JS file inside a Web Worker:

#### Option A: `importScripts()` (Classic Worker)

```typescript
// For type: 'classic' workers
importScripts('/engine/engine.js');
// Now `createChessEngine` is available globally
const Module = await createChessEngine({ locateFile: (p) => `/engine/${p}` });
```

#### Option B: Dynamic `import()` with `webpackIgnore` (Module Worker)

```typescript
// For type: 'module' workers (preferred)
// webpackIgnore prevents the bundler from trying to bundle the Emscripten output
const { default: createChessEngine } = await import(
  /* webpackIgnore: true */ '/engine/engine.js'
);
const Module = await createChessEngine({ locateFile: (p) => `/engine/${p}` });
```

#### Option C: `fetch` + `eval` (Fallback)

```typescript
// Last resort if imports don't work
const response = await fetch('/engine/engine.js');
const code = await response.text();
const blob = new Blob([code], { type: 'application/javascript' });
const url = URL.createObjectURL(blob);
const module = await import(url);
```

**Recommendation**: Use **Option B** (dynamic import with `webpackIgnore`) for module workers. This is the cleanest approach and works with both webpack and Turbopack.

---

## 5. Comlink vs Raw postMessage

### Recommendation: **Use Comlink**

| Aspect | Comlink | Raw postMessage |
|--------|---------|-----------------|
| **Boilerplate** | Minimal — expose/wrap functions | Significant — message types, handlers, routing |
| **Type Safety** | Excellent with TypeScript generics | Manual type guards needed |
| **DX** | Feels like calling local async functions | Complex event-driven patterns |
| **Performance overhead** | Negligible (~0.01ms per call) | None |
| **Bundle size** | ~1.1KB gzipped | 0 KB |
| **Transferables** | Supported via `Comlink.transfer()` | Supported natively |
| **Error handling** | Automatic — rejects promise on worker error | Manual — must serialize errors |
| **Proxy support** | Can proxy entire classes/objects | Not applicable |
| **Maturity** | Google Chrome Labs, well-maintained | Native API |

### Why Comlink Wins for This Use Case

1. **Chess engine has a clean function-based API** — `searchBestMove(depth)`, `evaluatePosition()`, etc. Comlink makes these feel like normal async calls.
2. **No high-frequency messaging** — Chess moves are infrequent (seconds between calls). The ~0.01ms Comlink overhead is irrelevant.
3. **Type safety** — With TypeScript, Comlink provides `Comlink.Remote<T>` that automatically types the proxied API.
4. **Error propagation** — If the C engine crashes or returns an error, Comlink automatically rejects the promise on the main thread.

### When NOT to Use Comlink

- Real-time audio/video processing (thousands of messages/sec)
- Transferring large ArrayBuffers frequently (use raw `postMessage` with `Transferable`)
- If you need to avoid any dependencies whatsoever

### Installation

```bash
pnpm add comlink
```

---

## 6. Turbopack + WASM Gotchas

### Known Issues (as of Next.js 16.x / March 2026)

1. **No `asyncWebAssembly` experiment in Turbopack**: Unlike webpack, Turbopack does not support `config.experiments.asyncWebAssembly`. Direct `.wasm` imports (`import wasm from './module.wasm'`) will fail.

2. **Static `.wasm` file imports as assets**: Turbopack does not support importing `.wasm` files as static asset URLs the way webpack does with custom loaders. See [vercel/next.js#75430](https://github.com/vercel/next.js/discussions/75430).

3. **Workaround — Use `public/` directory**: Place `.wasm` files in `public/` and load them via `fetch()` or let Emscripten's `locateFile` resolve them. This bypasses all bundler issues.

4. **Worker support works**: The `new Worker(new URL(...), { type: 'module' })` pattern IS supported by Turbopack. Web Workers themselves are fine.

5. **Custom webpack config is ignored by Turbopack**: Any `webpack:` configuration in `next.config.mjs` is silently ignored when using Turbopack. Don't rely on webpack loaders for WASM.

### Turbopack Configuration (for custom rules)

Turbopack does support some configuration via `next.config.mjs`:

```javascript
// next.config.mjs
/** @type {import('next').NextConfig} */
const nextConfig = {
  // Turbopack-specific configuration
  turbopack: {
    rules: {
      // Custom rules if needed for other file types
      '*.svg': {
        loaders: ['@svgr/webpack'],
        as: '*.js',
      },
    },
  },
};

export default nextConfig;
```

**There is currently no Turbopack rule for `.wasm` files.** The `public/` directory approach is the way to go.

### Our Strategy: Bypass Bundler Entirely

Since we're using Emscripten (which has its own JS glue + WASM loading), we don't need the bundler to process WASM at all:

```
Build step: emcc engine.c → public/engine/engine.js + public/engine/engine.wasm
Runtime:    Web Worker → fetch/import from /engine/ → instantiate WASM
```

This approach is **bundler-agnostic** and works with webpack, Turbopack, or any future bundler.

---

## 7. chess.js Library Status

### Current Status (March 2026)

- **Package**: `chess.js` on npm
- **Repository**: [github.com/jhlywa/chess.js](https://github.com/jhlywa/chess.js)
- **Latest version**: ~1.0.x (TypeScript rewrite)
- **Last published**: 2025 (actively maintained)
- **Language**: TypeScript (rewritten from JavaScript in v1.0)
- **License**: BSD-2-Clause
- **Weekly downloads**: High (one of the most popular chess libraries on npm)

### What chess.js Provides

- Move generation and validation
- FEN parsing and generation
- PGN import/export
- Check/checkmate/stalemate/draw detection
- Legal move listing
- Board state management

### What chess.js Does NOT Provide

- AI / move search (no engine)
- Evaluation functions
- Opening books
- Endgame tablebases

### Can It Work Alongside a Custom Engine?

**Yes, absolutely.** This is in fact the ideal architecture:

```
chess.js  →  Game rules, move validation, FEN/PGN, UI state
WASM engine  →  Position evaluation, best move search (AI)
```

The two complement each other perfectly:
- Use `chess.js` on the **main thread** for UI state management, move validation, legal move highlighting, and game history
- Use the **WASM engine** in a **Web Worker** for AI search/evaluation

### Integration Pattern

```typescript
import { Chess } from 'chess.js';

const game = new Chess();

// User makes a move — validated by chess.js
const move = game.move({ from: 'e2', to: 'e4' });
if (!move) {
  console.error('Illegal move');
  return;
}

// Send position to WASM engine for AI response
const fen = game.fen();
const bestMove = await engineWorker.searchBestMove(fen, 6, 5000);

// Apply AI's response — again validated by chess.js
game.move(bestMove); // e.g., "e7e5" or { from: 'e7', to: 'e5' }
```

### Installation

```bash
pnpm add chess.js
```

---

## 8. Recommended Architecture

### System Diagram

```
┌─────────────────────────────────────────┐
│            Next.js App (Main Thread)     │
│                                         │
│  ┌─────────────┐    ┌───────────────┐  │
│  │  chess.js    │    │  React UI     │  │
│  │  (game      │◄──►│  (board,      │  │
│  │   state)    │    │   controls)   │  │
│  └──────┬──────┘    └───────────────┘  │
│         │                               │
│         │ FEN string                    │
│         ▼                               │
│  ┌─────────────────┐                   │
│  │  useChessEngine  │  (React Hook)    │
│  │  + Comlink proxy │                  │
│  └────────┬────────┘                   │
│           │ Comlink RPC                 │
├───────────┼─────────────────────────────┤
│           │        Web Worker           │
│           ▼                             │
│  ┌─────────────────┐                   │
│  │  Engine Worker   │                  │
│  │  (Comlink.expose)│                  │
│  └────────┬────────┘                   │
│           │                             │
│           ▼                             │
│  ┌─────────────────┐                   │
│  │  WASM Module     │                  │
│  │  (chess engine)  │                  │
│  │  via Emscripten  │                  │
│  └─────────────────┘                   │
└─────────────────────────────────────────┘
```

### Data Flow

1. **User clicks a square** → React updates `chess.js` game state
2. **After user move** → `useChessEngine` hook sends FEN to worker via Comlink
3. **Worker receives FEN** → Passes to WASM engine via `cwrap`'d function
4. **Engine computes** → Returns best move as UCI string (e.g., "e2e4")
5. **Worker returns result** → Comlink resolves promise on main thread
6. **React updates** → `chess.js` applies AI move, UI re-renders

---

## 9. Complete Code Patterns

### 9.1 Emscripten Build Script

```bash
#!/bin/bash
# scripts/build-engine.sh

set -e

SRCDIR="engine"
OUTDIR="public/engine"

mkdir -p $OUTDIR

# Production build
emcc \
  $SRCDIR/engine.c \
  $SRCDIR/search.c \
  $SRCDIR/evaluate.c \
  $SRCDIR/movegen.c \
  -o $OUTDIR/engine.js \
  -O3 \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="createChessEngine" \
  -s EXPORTED_FUNCTIONS='["_init_engine","_set_position","_search_best_move","_evaluate_position","_get_legal_moves","_make_move"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
  -s ENVIRONMENT='web,worker' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=16777216 \
  -s NO_EXIT_RUNTIME=1 \
  -s FILESYSTEM=0 \
  --no-entry

echo "Engine built successfully → $OUTDIR/"
ls -la $OUTDIR/
```

### 9.2 TypeScript Type Definitions

```typescript
// src/types/engine.ts

export interface ChessEngineAPI {
  initEngine: () => void;
  setPosition: (fen: string) => number;
  searchBestMove: (depth: number, timeMs: number) => string;
  evaluatePosition: () => number;
  getLegalMoves: () => string;
  makeMove: (moveUci: string) => number;
}

export interface EngineSearchResult {
  bestMove: string;      // UCI format: "e2e4"
  evaluation: number;    // centipawns
  depth: number;
  timeMs: number;
}
```

### 9.3 Web Worker with Comlink

```typescript
// src/workers/chess-engine.worker.ts
/// <reference lib="webworker" />

import * as Comlink from 'comlink';
import type { ChessEngineAPI } from '@/types/engine';

let api: ChessEngineAPI | null = null;
let isReady = false;

const engineService = {
  async init(): Promise<boolean> {
    if (isReady) return true;

    try {
      // Load the Emscripten module from public/
      // Using self.importScripts for broad compatibility in workers
      // @ts-expect-error - dynamic import from public dir
      const createChessEngine = (await import(/* webpackIgnore: true */ '/engine/engine.js')).default;

      const Module = await createChessEngine({
        locateFile: (path: string) => `/engine/${path}`,
      });

      api = {
        initEngine: Module.cwrap('init_engine', null, []),
        setPosition: Module.cwrap('set_position', 'number', ['string']),
        searchBestMove: Module.cwrap('search_best_move', 'string', ['number', 'number']),
        evaluatePosition: Module.cwrap('evaluate_position', 'number', []),
        getLegalMoves: Module.cwrap('get_legal_moves', 'string', []),
        makeMove: Module.cwrap('make_move', 'number', ['string']),
      };

      api.initEngine();
      isReady = true;
      return true;
    } catch (err) {
      console.error('Failed to initialize chess engine:', err);
      return false;
    }
  },

  async searchBestMove(fen: string, depth: number, timeMs: number): Promise<string> {
    if (!api) throw new Error('Engine not initialized');
    api.setPosition(fen);
    return api.searchBestMove(depth, timeMs);
  },

  async evaluatePosition(fen: string): Promise<number> {
    if (!api) throw new Error('Engine not initialized');
    api.setPosition(fen);
    return api.evaluatePosition();
  },

  async getLegalMoves(fen: string): Promise<string[]> {
    if (!api) throw new Error('Engine not initialized');
    api.setPosition(fen);
    const movesStr = api.getLegalMoves();
    return movesStr ? movesStr.split(',') : [];
  },

  async isReady(): Promise<boolean> {
    return isReady;
  },
};

export type EngineService = typeof engineService;

Comlink.expose(engineService);
```

### 9.4 React Hook

```typescript
// src/hooks/useChessEngine.ts
'use client';

import { useEffect, useRef, useState, useCallback } from 'react';
import * as Comlink from 'comlink';
import type { EngineService } from '@/workers/chess-engine.worker';

export function useChessEngine() {
  const workerRef = useRef<Worker | null>(null);
  const apiRef = useRef<Comlink.Remote<EngineService> | null>(null);
  const [isReady, setIsReady] = useState(false);
  const [isThinking, setIsThinking] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const worker = new Worker(
      new URL('../workers/chess-engine.worker.ts', import.meta.url),
      { type: 'module' }
    );

    workerRef.current = worker;
    const api = Comlink.wrap<EngineService>(worker);
    apiRef.current = api;

    // Initialize the engine
    api.init().then((success) => {
      if (success) {
        setIsReady(true);
      } else {
        setError('Failed to initialize chess engine');
      }
    }).catch((err) => {
      setError(err.message);
    });

    return () => {
      worker.terminate();
      workerRef.current = null;
      apiRef.current = null;
    };
  }, []);

  const searchBestMove = useCallback(
    async (fen: string, depth: number = 6, timeMs: number = 5000): Promise<string | null> => {
      if (!apiRef.current || !isReady) return null;

      setIsThinking(true);
      try {
        const bestMove = await apiRef.current.searchBestMove(fen, depth, timeMs);
        return bestMove;
      } catch (err) {
        setError(err instanceof Error ? err.message : 'Search failed');
        return null;
      } finally {
        setIsThinking(false);
      }
    },
    [isReady]
  );

  const evaluatePosition = useCallback(
    async (fen: string): Promise<number | null> => {
      if (!apiRef.current || !isReady) return null;
      try {
        return await apiRef.current.evaluatePosition(fen);
      } catch {
        return null;
      }
    },
    [isReady]
  );

  return {
    isReady,
    isThinking,
    error,
    searchBestMove,
    evaluatePosition,
  };
}
```

### 9.5 Next.js Configuration

```javascript
// next.config.mjs
/** @type {import('next').NextConfig} */
const nextConfig = {
  // No special WASM config needed since we use public/ directory approach
  // This config works with both webpack and Turbopack

  // Ensure .wasm files are served with correct MIME type
  async headers() {
    return [
      {
        source: '/engine/:path*.wasm',
        headers: [
          {
            key: 'Content-Type',
            value: 'application/wasm',
          },
          {
            key: 'Cache-Control',
            value: 'public, max-age=31536000, immutable',
          },
        ],
      },
    ];
  },
};

export default nextConfig;
```

### 9.6 TypeScript Worker Declarations

```typescript
// src/types/worker.d.ts

// Ensure TypeScript recognizes the Worker constructor pattern
declare module '*.worker.ts' {
  const WorkerFactory: new () => Worker;
  export default WorkerFactory;
}
```

### 9.7 Usage in a Component

```typescript
// src/components/ChessGame.tsx
'use client';

import { useState, useCallback } from 'react';
import { Chess } from 'chess.js';
import { useChessEngine } from '@/hooks/useChessEngine';

export default function ChessGame() {
  const [game, setGame] = useState(new Chess());
  const { isReady, isThinking, searchBestMove } = useChessEngine();

  const makePlayerMove = useCallback(
    async (from: string, to: string) => {
      const gameCopy = new Chess(game.fen());
      const move = gameCopy.move({ from, to, promotion: 'q' });

      if (!move) return; // illegal move

      setGame(gameCopy);

      // Get AI response
      if (isReady && !gameCopy.isGameOver()) {
        const bestMove = await searchBestMove(gameCopy.fen(), 6, 5000);
        if (bestMove) {
          const aiGame = new Chess(gameCopy.fen());
          aiGame.move(bestMove);
          setGame(aiGame);
        }
      }
    },
    [game, isReady, searchBestMove]
  );

  return (
    <div>
      {/* Chess board UI */}
      {isThinking && <div>Engine is thinking...</div>}
      {!isReady && <div>Loading engine...</div>}
    </div>
  );
}
```

---

## Summary of Answers

### What Emscripten flags produce the cleanest WASM module for browser use?

`-O3 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME="createChessEngine" -s ENVIRONMENT='web,worker' -s FILESYSTEM=0 -s ALLOW_MEMORY_GROWTH=1 --no-entry` with explicit `EXPORTED_FUNCTIONS` and `EXPORTED_RUNTIME_METHODS`.

### How does Next.js App Router handle WASM file serving?

**Use the `public/` directory.** Place both the `.js` glue code and `.wasm` binary in `public/engine/`. This works with all bundlers and avoids any webpack/Turbopack configuration. The files are served as static assets with automatic caching.

### Best pattern for Web Worker in Next.js with TypeScript?

Use `new Worker(new URL('../workers/file.worker.ts', import.meta.url), { type: 'module' })` inside a `useEffect` hook. This pattern is supported by both webpack and Turbopack and handles TypeScript compilation automatically.

### Should we use Comlink or raw postMessage?

**Use Comlink.** For a chess engine with a function-based API and infrequent calls (seconds between moves), Comlink's ergonomics far outweigh its negligible overhead (~0.01ms per call, ~1.1KB gzipped). It provides excellent TypeScript integration and automatic error propagation.

### Any Next.js config needed for WASM support?

**Minimal.** Since we use the `public/` directory approach, no webpack/Turbopack WASM configuration is needed. Optionally add `headers()` in `next.config.mjs` to set proper MIME types and caching for `.wasm` files.

---

## Sources

- [Emscripten: Interacting with Code](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html) — Official docs on ccall, cwrap, EXPORTED_FUNCTIONS
- [Emscripten: Modularized Output](https://emscripten.org/docs/compiling/Modularized-Output.html) — MODULARIZE flag documentation
- [Emscripten: Settings Reference](https://emscripten.org/docs/tools_reference/settings_reference.html) — Complete compiler settings
- [Emscripten: Building to WebAssembly](https://emscripten.org/docs/compiling/WebAssembly.html) — WASM compilation overview
- [V8 Blog: Standalone WebAssembly Binaries](https://v8.dev/blog/emscripten-standalone-wasm) — Background on standalone WASM
- [Web Workers in Next.js 15 with Comlink](https://park.is/blog_posts/20250417_nextjs_comlink_examples/) — Comprehensive Comlink + Next.js guide
- [Comlink GitHub](https://github.com/GoogleChromeLabs/comlink) — Official Comlink repository
- [Next.js: Turbopack Config](https://nextjs.org/docs/app/api-reference/config/next-config-js/turbopack) — Turbopack configuration reference
- [Loading .wasm files with Turbopack (Discussion #75430)](https://github.com/vercel/next.js/discussions/75430) — Known Turbopack WASM limitations
- [Next.js: Public Folder](https://nextjs.org/docs/app/api-reference/file-conventions/public-folder) — Static asset serving
- [chess.js GitHub](https://github.com/jhlywa/chess.js) — chess.js library
- [Vercel: Using WebAssembly](https://vercel.com/docs/functions/runtimes/wasm) — Vercel's WASM documentation
