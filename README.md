# Claudefish

A chess engine written from scratch in C, compiled to WebAssembly, and playable in the browser through a Next.js interface. No Stockfish code — this is an original engine targeting ~2800-3200 ELO classical strength.

## Features

### Engine

- **Bitboard representation** with magic bitboards for sliding piece attack generation
- **Alpha-beta with PVS** and iterative deepening
- **Quiescence search** to avoid horizon effects
- **Null move pruning** and **Late Move Reductions (LMR)**
- **Transposition tables** with Zobrist hashing
- **Move ordering**: TT move, MVV-LVA captures, killer moves, history heuristic
- **Tapered evaluation** (middlegame/endgame interpolation): material, piece-square tables, pawn structure, king safety, mobility
- **Polyglot opening book** for varied play
- **Draw detection**: threefold repetition, fifty-move rule, insufficient material, stalemate
- **Time management** with fixed depth and fixed time modes

### Interface

- Drag-and-drop and click-to-move interaction with legal move indicators
- 4 difficulty levels (Easy through Maximum) controlling search depth and time
- Move history in algebraic notation
- Evaluation bar alongside the board
- Inline captured pieces on player bars
- Undo, new game, and FEN position loading (Alt+Shift+F)
- 3 themes: Classic Wood, Dark Marble, Light Minimalist
- Responsive layout for desktop, tablet, and mobile

## Tech Stack

| Layer | Technology |
|---|---|
| Engine | C (original) |
| Compilation | Emscripten -> WebAssembly |
| Frontend | Next.js 16 (App Router, Turbopack) |
| Language | TypeScript |
| Game Logic | chess.js |
| Worker Bridge | Comlink |
| Concurrency | Web Workers |

## Getting Started

### Prerequisites

- Git
- Node.js 18+
- pnpm

### Setup

```bash
# Clone and enter the project
git clone git@github.com:aravhawk/claudefish.git
cd claudefish

# Install Emscripten SDK + pnpm dependencies
.factory/init.sh

# Build the WASM engine
source ./emsdk/emsdk_env.sh && ./scripts/build-engine.sh

# Start the dev server
pnpm dev --turbopack --port 3001
```

Open [http://localhost:3001](http://localhost:3001) in your browser.

## Project Structure

```
claudefish/
├── engine/
│   ├── src/              # C engine source (bitboard, search, eval, etc.)
│   ├── tests/            # C test suite (perft, search, evaluation)
│   └── book/             # Polyglot opening book (rodent.bin)
├── src/
│   ├── app/              # Next.js App Router (page, layout, themes, game logic)
│   ├── components/
│   │   ├── Board/        # Chess board rendering, drag/drop, promotion
│   │   ├── GameOverOverlay/
│   │   └── MoveHistory/
│   ├── hooks/            # useChessEngine hook (Comlink + Worker lifecycle)
│   ├── types/            # Engine type definitions
│   └── workers/          # Web Worker for WASM engine
├── public/engine/        # Compiled WASM artifacts (engine.js + engine.wasm)
├── scripts/
│   └── build-engine.sh   # Emscripten build script
└── tests/                # Integration tests (WASM, game utils, board, themes)
```

## Architecture

```
┌─────────────────────────────────┐
│        Next.js Frontend         │
│  (React, chess.js, TypeScript)  │
├─────────────────────────────────┤
│         WASM Bridge             │
│  (Web Worker + Comlink + WASM)  │
├─────────────────────────────────┤
│          C Engine               │
│  (Bitboards, Search, Eval)      │
└─────────────────────────────────┘
```

1. User moves a piece -> chess.js validates -> board re-renders
2. Current FEN is sent to the Web Worker via Comlink
3. Worker calls WASM: `set_position(fen)` then `search_best_move(depth, time_ms)`
4. Engine returns the best move as a UCI string (e.g. `e2e4`)
5. chess.js applies the engine's move -> board re-renders

The engine never runs on the main thread. Game state is derived from a `(baseFen, playedMoves[])` pair — the `Chess` instance is always rebuilt by replaying moves, never stored as state.

## Testing

```bash
# Integration & frontend tests (36 tests)
pnpm test

# TypeScript type checking
pnpm typecheck

# Linting
pnpm lint

# C engine tests
gcc -O2 -o /tmp/claudefish_test engine/src/*.c engine/tests/*.c -lm && /tmp/claudefish_test
```
