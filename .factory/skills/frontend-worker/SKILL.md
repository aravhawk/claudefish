---
name: frontend-worker
description: Builds the Next.js chess website UI for Claudefish — board component, drag-and-drop piece interaction, game features (new game, undo, flip board, difficulty), themes, styling, and responsive layout. Uses chess.js for game state and the useChessEngine hook for engine integration.
---

# Frontend Worker

You are the frontend developer for Claudefish. You build the Next.js chess website UI including the interactive chessboard, game controls, move history, evaluation display, themes, and responsive styling. You use chess.js for game state management and the `useChessEngine` hook (built by the integration worker) for engine communication.

## When to Use This Skill

- When the orchestrator assigns UI/frontend tasks for the chess website
- When building or modifying: chessboard component, piece interaction, game controls, move history, evaluation bar, settings panel, themes, or layout
- When implementing drag-and-drop, click-to-move, or other interactive features
- When styling, theming, or making the UI responsive
- When debugging or verifying visual/interactive behavior in the browser

## Required Skills

- **agent-browser** — Used for manual verification of all UI features. Every interactive feature must be visually confirmed via agent-browser.

## Reference Documents

Before starting work, read these documents:

- `.factory/library/architecture.md` — Overall system architecture and component hierarchy
- Integration hook: `src/hooks/useChessEngine.ts` — API surface for engine communication
- Integration types: `src/types/engine.ts` — TypeScript interfaces for engine data

## Work Procedure

### 1. Understand the Task (Read)

- **Read** the task description from the orchestrator carefully
- **Read** `.factory/library/architecture.md` for component hierarchy and design decisions
- **Read** existing UI components in `src/components/` and pages in `src/app/` to understand current state
- **Read** `src/hooks/useChessEngine.ts` to understand the engine integration API
- **Read** `src/types/engine.ts` for TypeScript interfaces
- **Read** `package.json` to check available dependencies and what styling solution is set up (Tailwind, CSS Modules, etc.)

### 2. Plan the Component Structure (Think)

Before writing code, plan:
- Which components need to be created or modified
- Props and state for each component
- How chess.js game state flows through the component tree
- How the `useChessEngine` hook connects to the UI
- Responsive breakpoints and layout strategy

### 3. Implement Components (Create / Edit)

- **Create** or **Edit** React components in `src/components/`:
  - `Board.tsx` — The chessboard grid, square highlighting, piece rendering
  - `Piece.tsx` — Individual piece with drag-and-drop support
  - `GameControls.tsx` — New game, undo, redo, flip board, resign buttons
  - `MoveHistory.tsx` — Scrollable move list in algebraic notation
  - `EvalBar.tsx` — Visual evaluation bar showing engine assessment
  - `SettingsPanel.tsx` — Difficulty, time control, theme selection
  - `GameStatus.tsx` — Check, checkmate, stalemate, draw indicators

- Use chess.js for all game logic:
  ```typescript
  import { Chess } from 'chess.js';
  ```
  - Validate moves with `chess.move()`
  - Get legal moves with `chess.moves({ square, verbose: true })`
  - Check game state with `chess.isCheckmate()`, `chess.isDraw()`, etc.

- Use the `useChessEngine` hook for engine interaction:
  ```typescript
  const { isReady, isSearching, bestMove, evaluation, search, stop } = useChessEngine();
  ```

- Implement drag-and-drop:
  - Use HTML5 Drag and Drop API or a library like @dnd-kit
  - Highlight legal move squares on drag start
  - Snap piece to target square on valid drop
  - Animate piece return on invalid drop
  - Support click-to-move as an alternative

- Check what styling solution is configured (Tailwind CSS or CSS Modules) and use it consistently

### 4. Build Pages and Layout (Create / Edit)

- **Create** or **Edit** the main game page in `src/app/page.tsx`:
  - Compose the board, controls, move history, and eval bar
  - Manage top-level game state with chess.js
  - Connect engine via `useChessEngine`
  - Handle responsive layout (board scales, sidebar collapses on mobile)

- **Edit** `src/app/layout.tsx` for global styles, fonts, and metadata
- **Create** additional pages if needed (e.g., `src/app/settings/page.tsx`)

### 5. Verify with Dev Server (Execute + agent-browser)

Start the development server:

```bash
cd web && pnpm dev
```

Then use **agent-browser** to verify every interactive feature:

- **agent-browser**: Navigate to `http://localhost:3001`
- **agent-browser**: Verify the chessboard renders correctly with all 32 pieces in starting position
- **agent-browser**: Test drag-and-drop — drag e2 pawn to e4, confirm it moves
- **agent-browser**: Test click-to-move — click a piece, see highlighted squares, click target
- **agent-browser**: Verify illegal moves are rejected (try moving a pawn backwards)
- **agent-browser**: Test game controls — new game resets board, undo reverts last move
- **agent-browser**: Verify engine plays a response after the player moves
- **agent-browser**: Check responsive layout at different viewport widths
- **agent-browser**: Verify theme switching if implemented
- **agent-browser**: Test game-over states — checkmate, stalemate display correctly
- **agent-browser**: Take a screenshot of the final UI for the report

### 6. Run Build and Lint (Execute)

```bash
cd web && pnpm build
cd web && pnpm lint
```

- **Execute** the build to catch any TypeScript or compilation errors
- **Execute** lint to ensure code quality
- Fix any errors before completing

### 7. Report Results

Summarize what was built and verified. Include:
- Files created or modified
- Features implemented and verified via agent-browser
- Screenshots or descriptions of UI state
- Any known limitations or next steps

## Example Handoff

```json
{
  "skill": "frontend-worker",
  "task": "Build the main chess game page with interactive board, drag-and-drop, move history, game controls, and engine integration",
  "actions_taken": [
    "Read .factory/library/architecture.md for component hierarchy",
    "Read src/hooks/useChessEngine.ts — hook exposes: isReady, isSearching, bestMove, evaluation, search(depth), stop(), setPosition(fen)",
    "Read src/types/engine.ts for SearchResult, EvaluationInfo interfaces",
    "Read package.json — confirmed Tailwind CSS is configured, chess.js 1.0.0-beta.8 installed, added @dnd-kit/core and @dnd-kit/utilities",
    "Created src/components/Board.tsx — 8x8 grid with alternating light/dark squares, coordinate labels, legal move dot indicators, last-move highlighting, check highlighting on king square",
    "Created src/components/Piece.tsx — SVG piece images with @dnd-kit draggable, smooth drop animation, ghost piece on drag",
    "Created src/components/Square.tsx — Individual square component handling drop targets, highlighting states (selected, legal-move, last-move, check)",
    "Created src/components/MoveHistory.tsx — Scrollable list with move pairs in algebraic notation, clickable to jump to position, auto-scroll to latest move",
    "Created src/components/EvalBar.tsx — Vertical bar showing engine evaluation, smooth animated transitions, displays centipawn value or mate-in-N",
    "Created src/components/GameControls.tsx — New Game, Undo, Redo, Flip Board, Resign buttons with icons and keyboard shortcuts",
    "Created src/components/GameStatus.tsx — Displays check/checkmate/stalemate/draw with appropriate styling and messages",
    "Created src/components/SettingsPanel.tsx — Difficulty slider (depth 1-20), play as white/black toggle, theme selector dropdown",
    "Edited src/app/page.tsx — Composed all components, chess.js game state with useState, engine integration via useChessEngine, auto-engine-reply after player moves, responsive grid layout",
    "Edited src/app/layout.tsx — Added Inter font, dark mode meta, page title 'Claudefish'",
    "Created src/styles/board-themes.ts — Three themes: classic (brown/cream), blue (navy/light blue), green (dark green/light green) with piece set configuration",
    "Executed: cd web && pnpm dev --port 3001 — dev server started on localhost:3001",
    "agent-browser: Navigated to localhost:3001 — board renders correctly, all 32 pieces in starting position, responsive layout confirmed",
    "agent-browser: Dragged white e2 pawn to e4 — piece moved smoothly, legal move dots appeared on drag, square highlighted after drop",
    "agent-browser: Engine responded with e7-e5 after ~0.5s — bestMove state updated, piece animated to new position",
    "agent-browser: Tested click-to-move — clicked Nf3, saw green dots on f3 and other legal squares, clicked f3, knight moved",
    "agent-browser: Tried illegal move (pawn backward) — piece snapped back to original square, no state change",
    "agent-browser: Clicked Undo twice — both moves reverted, board back to starting position",
    "agent-browser: Clicked New Game — board reset, move history cleared, engine re-initialized",
    "agent-browser: Clicked Flip Board — board rotated 180°, black pieces now at bottom, coordinates updated",
    "agent-browser: Played Scholar's mate (e4, Bc4, Qh5, Qxf7#) — GameStatus showed 'Checkmate! White wins' with animation",
    "agent-browser: Tested at 375px viewport width — board scaled down, sidebar collapsed to bottom panel, all controls accessible",
    "agent-browser: Switched theme to 'Blue' — board colors changed to navy/light blue scheme instantly",
    "agent-browser: Screenshot captured of final UI state",
    "Executed: cd web && pnpm build — build succeeded, zero errors",
    "Executed: cd web && pnpm lint — no linting warnings or errors"
  ],
  "files_modified": [
    "web/package.json",
    "web/src/app/page.tsx",
    "web/src/app/layout.tsx",
    "web/src/components/Board.tsx",
    "web/src/components/Piece.tsx",
    "web/src/components/Square.tsx",
    "web/src/components/MoveHistory.tsx",
    "web/src/components/EvalBar.tsx",
    "web/src/components/GameControls.tsx",
    "web/src/components/GameStatus.tsx",
    "web/src/components/SettingsPanel.tsx",
    "web/src/styles/board-themes.ts"
  ],
  "test_results": {
    "nextjs_build": "success — zero errors",
    "nextjs_lint": "passed — zero warnings",
    "browser_verification": {
      "board_rendering": "pass — all pieces correct, squares aligned",
      "drag_and_drop": "pass — smooth drag, legal move highlighting, snap-to-square",
      "click_to_move": "pass — piece selection, legal move dots, move execution",
      "illegal_move_rejection": "pass — piece returns to origin",
      "engine_response": "pass — engine plays within 1s at depth 10",
      "game_controls": "pass — new game, undo, redo, flip all functional",
      "checkmate_detection": "pass — game-over banner displays correctly",
      "responsive_layout": "pass — tested at 375px, 768px, 1280px",
      "theme_switching": "pass — all three themes render correctly"
    }
  },
  "status": "complete",
  "notes": "All core UI features are implemented and verified via agent-browser. The board supports both drag-and-drop and click-to-move. Engine auto-replies after each player move. Three board themes are available. Layout is responsive down to 375px. Keyboard shortcuts: Ctrl+Z for undo, Ctrl+N for new game, F for flip. Move history supports click-to-jump for reviewing past positions."
}
```

## When to Return to Orchestrator

- When all assigned UI tasks are complete, verified via agent-browser, and the build passes
- When you need engine API changes → integration-worker
- When you need new engine features (stronger eval, opening book) → engine-worker
- When WASM loading fails or Worker communication breaks → integration-worker
- When you encounter design decisions not covered in the architecture document
