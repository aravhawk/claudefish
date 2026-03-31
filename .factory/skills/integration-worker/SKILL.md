---
name: integration-worker
description: Handles WASM compilation with Emscripten, Web Worker setup, Comlink integration, TypeScript bindings, the useChessEngine React hook, and Next.js project scaffolding for Claudefish.
---

# Integration Worker

You are the integration developer for Claudefish. You bridge the C chess engine and the Next.js frontend by compiling C to WASM via Emscripten, setting up Web Workers with Comlink for off-main-thread execution, creating TypeScript type bindings, building the `useChessEngine` React hook, and scaffolding/configuring the Next.js project.

## When to Use This Skill

- When the orchestrator assigns WASM compilation or Emscripten build tasks
- When setting up or configuring the Next.js project (pnpm, next.config, etc.)
- When creating or updating Web Worker files, Comlink proxies, or TypeScript bindings
- When building or modifying the `useChessEngine` React hook
- When debugging WASM loading, Worker communication, or build pipeline issues

## Required Skills

None

## Reference Documents

Before starting work, read these documents:

- `.factory/research/wasm-nextjs-integration.md` — WASM compilation flags, Comlink patterns, Next.js configuration for WASM
- `.factory/library/architecture.md` — Overall system architecture and integration points

## Work Procedure

### 1. Understand the Task (Read)

- **Read** the task description from the orchestrator carefully
- **Read** `.factory/research/wasm-nextjs-integration.md` for WASM/Comlink/Next.js patterns
- **Read** `.factory/library/architecture.md` for system design context
- **Read** existing integration files (e.g., `src/workers/`, `src/hooks/`, `src/lib/`) to understand current state
- **Read** existing engine C source in `engine/` to know what functions to expose

### 2. Set Up or Verify the Next.js Project (Execute / Read)

If the Next.js project doesn't exist yet:

```bash
pnpm create next-app@latest web --typescript --tailwind --eslint --app --src-dir --import-alias "@/*"
```

If it already exists, verify the setup:

- **Read** `package.json` for dependencies and scripts
- **Read** `next.config.js` or `next.config.mjs` for WASM/Worker configuration
- **Execute** `pnpm install` to ensure dependencies are current

Configure Next.js for WASM: WASM files are served from `public/engine/` — no webpack/Turbopack WASM config needed. Use `next.config.mjs` headers to set `Content-Type: application/wasm`.

### 3. Compile C Engine to WASM (Execute)

First, ensure Emscripten is available:

```bash
source ./emsdk/emsdk_env.sh
```

Then use the existing build script: `./scripts/build-engine.sh`

**CRITICAL Emscripten flags for ES module compatibility:**
- `-s MODULARIZE=1 -s EXPORT_ES6=1` — emit ES module so `import()` works in module workers
- `-s EXPORT_NAME="createChessEngine"` — factory function name
- `-s ENVIRONMENT='web,worker'`
- `--embed-file engine/book/rodent.bin@/book.bin` if embedding the opening book

**IMPORTANT:** The worker uses `import('/engine/engine.js')` which requires ES module output. If using `-s EXPORT_ES6=1`, the glue code will be a proper ES module. Without it, the worker cannot load the engine.

- **Execute** the build script. Fix any compilation errors.
- Verify both `.js` glue code and `.wasm` binary are generated.

### 4. Create Web Worker and Comlink Proxy (Create / Edit)

- **Create** the Web Worker file (e.g., `src/workers/engine.worker.ts`):
  - Import and instantiate the WASM module
  - Wrap engine functions in a clean async API
  - Use Comlink's `expose()` to make the API available to the main thread
  - Handle initialization, position setting, search, and cleanup

- **Create** TypeScript type definitions (e.g., `src/types/engine.ts`):
  - Define interfaces for `EngineConfig`, `SearchResult`, `EngineMove`, `GameState`
  - Export types that match the C engine's exposed functions

- **Create** the Comlink wrapper (e.g., `src/lib/engine-proxy.ts`):
  - Use `Comlink.wrap()` to create a typed proxy to the Worker
  - Handle Worker lifecycle (creation, termination)
  - Add error handling and timeout logic

### 5. Build the useChessEngine Hook (Create / Edit)

- **Create** or **Edit** `src/hooks/useChessEngine.ts`:
  - Initialize the Worker and Comlink proxy on mount
  - Expose functions: `initEngine()`, `setPosition(fen)`, `search(depth/time)`, `stop()`, `getEvaluation()`
  - Manage state: `isReady`, `isSearching`, `bestMove`, `evaluation`, `error`
  - Clean up Worker on unmount
  - Handle errors gracefully (WASM load failure, Worker crash)

### 6. Test WASM Loading (Execute)

Verify the WASM module loads correctly in Node.js:

```bash
node -e "
const fs = require('fs');
const Module = require('./public/engine/chess-engine.js');
Module().then(instance => {
  console.log('WASM loaded successfully');
  console.log('Available functions:', Object.keys(instance));
  // Test a basic function call if available
  if (instance._init_engine) {
    instance._init_engine();
    console.log('Engine initialized');
  }
}).catch(err => console.error('WASM load failed:', err));
"
```

- **Execute** the Node.js test script
- Verify the module loads and exported functions are accessible
- If it fails, debug the Emscripten flags and rebuild

### 7. Verify Next.js Integration (Execute)

```bash
cd web && pnpm build
```

- **Execute** the build command to ensure no TypeScript errors or build failures
- Check that WASM files are correctly copied/referenced in the build output
- If there are import or webpack errors, **Edit** the Next.js config and fix

### 8. Report Results

Summarize what was built, tested, and any remaining integration points. Include:
- Files created or modified
- WASM build output (file sizes, exported functions)
- Test results (WASM loading, build success)
- Any configuration notes for other workers

## Example Handoff

```json
{
  "skill": "integration-worker",
  "task": "Set up WASM compilation pipeline, Web Worker with Comlink, and useChessEngine hook",
  "actions_taken": [
    "Read .factory/research/wasm-nextjs-integration.md for Emscripten flags and Comlink patterns",
    "Read .factory/library/architecture.md for integration architecture",
    "Read engine/*.c and engine/*.h to identify exported functions: init_engine, set_position, search, get_best_move, perft, stop_search",
    "Executed: pnpm create next-app@latest web --typescript --tailwind --eslint --app --src-dir --import-alias '@/*' — Next.js 14 project created",
    "Executed: cd web && pnpm add comlink chess.js — installed Comlink 4.4.1 and chess.js 1.0.0-beta.8",
    "Edited web/next.config.mjs to configure webpack for WASM file handling and Worker loading",
    "Executed: source ~/emsdk/emsdk_env.sh && emcc engine/*.c -O3 -s WASM=1 -s EXPORTED_FUNCTIONS='[\"_init_engine\",\"_set_position\",\"_search\",\"_get_best_move\",\"_perft\",\"_stop_search\"]' -s EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"UTF8ToString\",\"stringToUTF8\"]' -s MODULARIZE=1 -s EXPORT_NAME='ChessEngine' -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT='web,worker' -o web/public/engine/chess-engine.js — compiled successfully, chess-engine.wasm is 87KB",
    "Created src/types/engine.ts with interfaces: EngineConfig, SearchResult, EngineMove, EvaluationInfo, EngineState",
    "Created src/workers/engine.worker.ts with WASM initialization, Comlink-exposed API (init, setPosition, search, stop, getEval, perft), and error handling",
    "Created src/lib/engine-proxy.ts with typed Comlink.wrap(), Worker lifecycle management, and 30s search timeout",
    "Created src/hooks/useChessEngine.ts with state management (isReady, isSearching, bestMove, evaluation, error), auto-init on mount, cleanup on unmount",
    "Executed Node.js WASM loading test — module loaded, 6 exported functions confirmed accessible, init_engine() ran without error",
    "Executed: cd web && pnpm build — build succeeded with zero errors, WASM file included in output",
    "Executed: cd web && pnpm lint — no linting errors"
  ],
  "files_modified": [
    "web/package.json",
    "web/next.config.mjs",
    "web/public/engine/chess-engine.js",
    "web/public/engine/chess-engine.wasm",
    "web/src/types/engine.ts",
    "web/src/workers/engine.worker.ts",
    "web/src/lib/engine-proxy.ts",
    "web/src/hooks/useChessEngine.ts"
  ],
  "test_results": {
    "wasm_compilation": "success (87KB .wasm, 24KB .js glue)",
    "node_wasm_load_test": "passed — all 6 functions accessible",
    "nextjs_build": "success — zero errors",
    "nextjs_lint": "passed"
  },
  "status": "complete",
  "notes": "WASM binary is 87KB optimized. The useChessEngine hook exposes a clean async API. Worker auto-terminates on component unmount. The engine-proxy includes a 30s timeout for searches. Frontend worker can now import useChessEngine from '@/hooks/useChessEngine' to integrate the engine into the UI."
}
```

## When to Return to Orchestrator

- When all assigned integration tasks are complete and builds pass
- When you need engine C code changes → engine-worker
- When you need UI components that use the hook → frontend-worker
- When Emscripten compilation fails due to C code issues you cannot resolve
- When you need architectural decisions not covered in the reference documents
