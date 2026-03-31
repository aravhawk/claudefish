# Architecture

## System Overview

Three layers: **C Engine → WASM Bridge → Next.js Frontend**

The chess engine is written in C for maximum performance, compiled to WebAssembly via Emscripten, and embedded in a Next.js website. The engine runs in a Web Worker to keep the UI responsive.

## C Engine (`engine/`)

### Source (`engine/src/`)
All C source files implementing the chess engine:

- **Board Representation:** Bitboard-based with 12 bitboards (one per piece type/color) plus mailbox redundancy for O(1) piece-at-square lookups.
- **Attack Generation:** Magic bitboards for sliding piece (bishop, rook, queen) attack generation. Precomputed tables for knight and king attacks.
- **Move Generation:** Pseudo-legal move generation with post-generation legality filter. Includes castling, en passant, and promotions.
- **Make/Unmake:** Incremental Zobrist hash updates on make/unmake for efficient transposition table indexing. Copy-make or unmake-stack approach.
- **Search:** Alpha-beta with Principal Variation Search (PVS) and iterative deepening. Aspiration windows on root.
- **Search Enhancements:** Quiescence search, null move pruning, late move reductions (LMR), futility pruning.
- **Transposition Table:** Zobrist-indexed hash table storing best move, score, depth, and node type (exact/alpha/beta).
- **Move Ordering:** TT move first, then MVV-LVA for captures, killer moves for quiets, history heuristic for remaining moves.
- **Evaluation:** Tapered evaluation interpolating between middlegame and endgame scores based on material phase. Piece-square tables, pawn structure, king safety, mobility.
- **Opening Book:** Polyglot opening book reader for varied play in the opening phase.
- **Time Management:** Allocates time per move based on remaining time and increment. Supports fixed depth and fixed time modes.

### Tests (`engine/tests/`)
C test files covering:
- Perft tests (move generation correctness at various depths/positions)
- Search tests (known tactical positions, mate-in-N)
- Evaluation tests (sanity checks on evaluation scores)

## WASM Bridge

### Compilation
Emscripten compiles `engine/src/*.c` → `public/engine/engine.js` + `engine.wasm`. Output placed in `public/engine/` to be served as static assets (bypasses the Next.js bundler).

### Exported C API
- `init_engine()` — Initialize engine state, allocate transposition table
- `set_position(fen)` — Set board position from FEN string
- `search_best_move(depth, time_ms)` — Search and return best move in UCI notation
- `evaluate_position()` — Return static evaluation of current position (centipawns)
- `get_legal_moves()` — Return list of legal moves in UCI notation

### Web Worker (`src/workers/chess-engine.worker.ts`)
- Loads `/engine/engine.js` inside the worker with `importScripts()` and reads the `createChessEngine` factory from the worker global scope
- Comlink exposes a typed API to the main thread
- Handles init, position setting, search, and evaluation requests

### React Hook (`src/hooks/useChessEngine.ts`)
Provides a clean React interface:
- `isReady` — Whether the WASM module is loaded and initialized
- `isThinking` — Whether the engine is currently searching
- `searchBestMove(fen, depth, timeMs)` — Request best move for a position
- `evaluatePosition(fen)` — Request static evaluation

## Next.js Frontend (`src/`)

### Framework
- Next.js App Router with Turbopack for fast development builds

### Game Logic
- **chess.js** manages game state, move validation, and move history on the main thread
- chess.js is the single source of truth for the game state

### Board Component
- Renders an 8×8 grid with SVG/CSS chess pieces
- Drag-and-drop + click-to-move interaction
- Legal move highlighting on piece selection
- Last move highlighting and check indication

### Engine Integration
- Engine accessed via `useChessEngine` hook + Web Worker
- Non-blocking: UI stays responsive during engine computation

### Game Features
- Difficulty levels (control search depth and time allocation)
- Move history display with navigation
- Captured pieces display
- Undo move
- New game
- Evaluation bar showing engine assessment

### Themes
Three board themes:
1. **Classic Wood** — warm wood tones
2. **Dark Marble** — dark, elegant marble textures
3. **Light Minimalist** — clean, modern light design

### Styling
Premium/elegant styling with CSS modules or Tailwind CSS.

## Data Flow

1. **User interacts with board** → chess.js validates the move → board re-renders
2. **After user move** → current FEN sent to Web Worker via Comlink
3. **Worker calls WASM** → `set_position(fen)` then `search_best_move(depth, time_ms)`
4. **Engine searches** → returns best move as UCI string (e.g., `e2e4`)
5. **Worker returns move** to main thread via Comlink promise resolution
6. **chess.js applies engine move** → board re-renders showing both the user's and engine's moves

## Key Invariants

- **Engine never runs on main thread** — always in a Web Worker to prevent UI blocking
- **chess.js is the source of truth** for game state on the frontend; the engine is consulted only for move selection and evaluation
- **WASM module is stateful** — `set_position` must be called before each `search_best_move`
- **All WASM files served from `public/engine/`** — bypasses the Next.js bundler, loaded as static assets
