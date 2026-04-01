# Claudefish — A Chess Engine & Web Interface

A chess engine written from scratch in C, compiled to WebAssembly via Emscripten, and embedded in a premium Next.js chess-playing website. No Stockfish code — this is an original engine targeting ~2800–3200 ELO classical strength.

## Overview

Claudefish is a full-stack chess project consisting of two major components:

1. **A C chess engine** built from the ground up with modern techniques — bitboard representation, magic bitboards, PVS search, and tapered evaluation.
2. **A polished web interface** powered by Next.js, where users can play against the engine directly in their browser via WebAssembly.

The engine runs entirely client-side in a Web Worker, keeping the UI responsive while computing moves.

## Features

### Engine

- **Bitboard representation** with 12 bitboards (one per piece type/color) plus mailbox redundancy for O(1) lookups
- **Magic bitboards** for sliding piece attack generation
- **Alpha-beta with Principal Variation Search (PVS)** and iterative deepening
- **Quiescence search** to avoid horizon effects
- **Null move pruning** and **Late Move Reductions (LMR)** for search efficiency
- **Transposition tables** with Zobrist hashing (exact/alpha/beta node types)
- **Move ordering**: TT move → MVV-LVA captures → killer moves → history heuristic
- **Tapered evaluation** interpolating between middlegame and endgame scores:
  - Material values and piece-square tables
  - Pawn structure analysis
  - King safety
  - Mobility evaluation
- **Polyglot opening book** for varied opening play
- **Draw detection** (threefold repetition, fifty-move rule, insufficient material, stalemate)
- **Time management** with support for fixed depth and fixed time modes

### Website

- 2D chess board with **drag-and-drop** and **click-to-move** interaction
- **Legal move highlights** on piece selection
- **4 difficulty levels** controlling search depth and time allocation
- **Move history** displayed in algebraic notation
- **Captured pieces** display
- **Undo move** and **New Game** controls
- **Evaluation bar** showing the engine's assessment of the position
- **Thinking indicator** while the engine computes
- **3 premium themes**:
  - Classic Wood — warm wood tones
  - Dark Marble — dark, elegant marble textures
  - Light Minimalist — clean, modern light design
- **Responsive layout** adapting to different screen sizes

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Engine | C (original, no Stockfish code) |
| Compilation | Emscripten → WebAssembly |
| Frontend | Next.js 16 (App Router + Turbopack) |
| Language | TypeScript |
| Game Logic | chess.js |
| Worker Communication | Comlink |
| Concurrency | Web Workers |

## Getting Started

### Prerequisites

- Git
- Node.js 18+
- pnpm

### Setup

```bash
# 1. Clone the repository
git clone https://github.com/your-username/claudefish.git
cd claudefish

# 2. Run the init script (installs emsdk locally + pnpm dependencies)
.factory/init.sh

# 3. Build the engine (WASM)
source ./emsdk/emsdk_env.sh && ./scripts/build-engine.sh

# 4. Start the development server
pnpm dev --turbopack --port 3001

# 5. Open in your browser
# http://localhost:3001
```

## Project Structure

```
claudefish/
├── engine/
│   ├── src/           # C engine source files
│   │   ├── bitboard.c/h    # Bitboard utilities
│   │   ├── movegen.c/h     # Move generation (magic bitboards)
│   │   ├── search.c/h      # Alpha-beta / PVS search
│   │   ├── evaluate.c/h    # Tapered evaluation
│   │   ├── position.c/h    # Board state management
│   │   ├── tt.c/h          # Transposition table
│   │   ├── book.c/h        # Polyglot opening book
│   │   ├── draw.c/h        # Draw detection
│   │   ├── time.c/h        # Time management
│   │   ├── movorder.c/h    # Move ordering heuristics
│   │   ├── zobrist.c/h     # Zobrist hashing
│   │   ├── engine.c/h      # Top-level engine API
│   │   └── types.h         # Shared type definitions
│   ├── tests/         # C test files (perft, search, evaluation)
│   └── book/          # Opening book data
├── src/
│   ├── app/           # Next.js App Router pages and layouts
│   ├── components/    # React components
│   │   ├── Board/           # Chess board rendering
│   │   ├── CapturedPieces/  # Captured pieces display
│   │   ├── GameOverOverlay/ # Game over UI
│   │   └── MoveHistory/     # Move history panel
│   ├── hooks/         # React hooks
│   │   └── useChessEngine.ts  # Engine integration hook
│   ├── types/         # TypeScript type definitions
│   └── workers/       # Web Worker for WASM engine
├── public/engine/     # Compiled WASM artifacts (engine.js + engine.wasm)
├── scripts/           # Build scripts
│   └── build-engine.sh
├── tests/             # Integration and frontend tests
└── emsdk/             # Emscripten SDK (local install)
```

## Testing

### Engine Tests (C)

```bash
gcc -O2 -o /tmp/claudefish_test engine/src/*.c engine/tests/*.c -lm && /tmp/claudefish_test
```

### Integration & Frontend Tests

```bash
pnpm test
```

### TypeScript Type Checking

```bash
pnpm typecheck
```

### Linting

```bash
pnpm lint
```

## Architecture

Claudefish uses a **three-layer architecture**:

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

**Data flow:**

1. User interacts with the board → **chess.js** validates the move → board re-renders
2. After the user's move, the current FEN is sent to the **Web Worker** via Comlink
3. The Worker calls the **WASM engine**: `set_position(fen)` → `search_best_move(depth, time_ms)`
4. The engine searches and returns the best move as a UCI string (e.g., `e2e4`)
5. The Worker returns the move to the main thread via Comlink promise resolution
6. **chess.js** applies the engine's move → board re-renders

**Key invariants:**

- The engine never runs on the main thread — always in a Web Worker
- chess.js is the source of truth for game state on the frontend
- The WASM module is stateful — `set_position` must precede each search
- All WASM files are served from `public/engine/` as static assets
