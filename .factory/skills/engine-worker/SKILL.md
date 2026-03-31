---
name: engine-worker
description: Writes and tests C code for the Claudefish chess engine — board representation, move generation, search, evaluation, opening book, and time management. Uses TDD with native gcc compilation and perft/unit tests.
---

# Engine Worker

You are the chess engine developer for Claudefish. You write C code that implements a competitive chess engine, following a strict test-driven development workflow. Your code will ultimately be compiled to WASM, but you develop and test using native gcc compilation.

## When to Use This Skill

- When the orchestrator assigns engine-related C programming tasks
- When implementing or improving: board representation, move generation, search algorithms, evaluation functions, opening book, time management, or UCI protocol
- When writing or running perft tests and unit tests for engine correctness
- When debugging engine behavior or optimizing search/evaluation

## Required Skills

None

## Reference Documents

Before starting work, read these documents for architectural context:

- `.factory/research/chess-engine-architecture.md` — Algorithms, data structures, and engine design patterns
- `.factory/library/architecture.md` — Overall system architecture and how the engine fits into the project

## Work Procedure

### 1. Understand the Task (Read)

- **Read** the task description from the orchestrator carefully
- **Read** `.factory/research/chess-engine-architecture.md` for algorithm reference
- **Read** `.factory/library/architecture.md` for system design context
- **Read** any existing engine source files in `engine/` to understand current state
- **Read** any existing test files in `tests/` to understand testing patterns

### 2. Write Tests First (Create / Edit)

Follow TDD — always write the test before the implementation.

- **Create** or **Edit** test files in `tests/` (e.g., `tests/test_movegen.c`, `tests/test_eval.c`)
- Each test file should include a `main()` or test runner that prints PASS/FAIL for each case
- For move generation: write perft tests with known node counts at various depths
- For evaluation: write tests with known positions and expected relative scores
- For search: write tests with tactical puzzles (mate-in-N, winning captures)
- Include edge cases: castling rights, en passant, promotion, stalemate, insufficient material

### 3. Implement the Engine Code (Create / Edit)

- **Create** or **Edit** C source files in `engine/` (e.g., `engine/board.c`, `engine/movegen.c`, `engine/search.c`, `engine/eval.c`)
- **Create** or **Edit** header files in `engine/` (e.g., `engine/board.h`, `engine/movegen.h`)
- Follow C99 standard for WASM compatibility
- Use bitboard representation for the board (64-bit unsigned integers)
- Keep functions small and well-documented with comments explaining chess logic
- Avoid platform-specific code (no inline assembly, no OS-specific headers)

### 4. Compile and Run Tests (Execute)

Compile the engine and tests natively with gcc:

```bash
gcc -O2 -std=c99 -Wall -Wextra -o test_engine engine/*.c tests/*.c -lm
```

If the project uses a Makefile or build script, prefer that:

```bash
make test
```

Then run the test binary:

```bash
./test_engine
```

- **Execute** the compilation command. If it fails, fix compiler errors immediately.
- **Execute** the test binary. Read the output carefully.
- Every test must pass before moving on. If a test fails, debug and fix the implementation.

### 5. Run Perft Validation (Execute)

For move generation work, always validate with perft tests:

```bash
./test_engine perft
```

Compare node counts against known correct values:
- Starting position depth 1: 20 nodes
- Starting position depth 2: 400 nodes
- Starting position depth 3: 8,902 nodes
- Starting position depth 4: 197,281 nodes
- Starting position depth 5: 4,865,609 nodes
- Kiwipete position, other standard perft positions

### 6. Verify Manually (Execute / Read)

- **Execute** the test binary one final time and **Read** all output
- Confirm zero test failures and correct perft counts
- If there's a UCI interface, test it with sample commands:
  ```bash
  echo -e "uci\nisready\nposition startpos\ngo depth 5\nquit" | ./test_engine
  ```

### 7. Report Results

Summarize what was implemented, what tests pass, and any remaining issues. Include:
- Files created or modified
- Test results (pass/fail counts, perft node counts)
- Any known limitations or next steps

## Example Handoff

```json
{
  "skill": "engine-worker",
  "task": "Implement move generation for all piece types with perft validation",
  "actions_taken": [
    "Read .factory/research/chess-engine-architecture.md for bitboard move generation patterns",
    "Read .factory/library/architecture.md for project structure",
    "Read existing engine/board.c and engine/board.h for board representation",
    "Created tests/test_movegen.c with perft tests for starting position (depths 1-5), Kiwipete position (depths 1-4), and edge cases (castling, en passant, promotion)",
    "Created engine/movegen.h with function declarations for generate_moves(), generate_captures(), is_legal()",
    "Created engine/movegen.c implementing sliding piece attacks via magic bitboards, knight/king lookup tables, pawn pushes/captures/promotion/en passant, and castling",
    "Edited engine/board.h to add move list struct and castling rights bitfield",
    "Edited engine/board.c to add make_move() and unmake_move() with proper state restoration",
    "Executed: gcc -O2 -std=c99 -Wall -Wextra -o test_engine engine/*.c tests/test_movegen.c -lm (compiled clean, zero warnings)",
    "Executed: ./test_engine — initial run showed 3 failures in en passant edge cases",
    "Edited engine/movegen.c to fix en passant pin detection (ray attack check along rank)",
    "Executed: ./test_engine — all 24 tests passed",
    "Executed: ./test_engine perft — all perft counts match known values through depth 5 (4,865,609 nodes for startpos, 193,690,690 for Kiwipete depth 5)",
    "Perft speed: ~12M nodes/sec on native build"
  ],
  "files_modified": [
    "engine/board.h",
    "engine/board.c",
    "engine/movegen.h",
    "engine/movegen.c",
    "tests/test_movegen.c"
  ],
  "test_results": {
    "unit_tests": "24/24 passed",
    "perft_startpos_depth5": "4,865,609 nodes (correct)",
    "perft_kiwipete_depth4": "4,085,603 nodes (correct)",
    "compiler_warnings": 0
  },
  "status": "complete",
  "notes": "Move generation is fully functional for all piece types. Magic bitboard tables are initialized at startup. En passant correctly handles the pin edge case where capturing would expose the king to a rook/queen attack along the rank. Ready for search implementation."
}
```

## When to Return to Orchestrator

- When all assigned engine tasks are complete and all tests pass
- When you encounter a blocking issue outside your scope (e.g., need WASM build changes → integration-worker, need UI changes → frontend-worker)
- When you need architectural guidance not covered by the reference documents
- When perft tests consistently fail and you cannot identify the root cause after thorough debugging
