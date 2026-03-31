# User Testing

## Validation Surface
- **Engine (Milestone 1):** Automated tests via native C compilation (gcc) + WASM integration tests (Node.js)
- **Website (Milestone 2):** agent-browser at http://localhost:3001
- **Tool:** agent-browser for all browser-based UI testing

## Validation Concurrency
- **agent-browser max concurrent validators:** 2
- **Rationale:** 18GB RAM, ~10GB used at baseline. Each agent-browser instance ~300MB + Next.js dev server ~400MB. 2 concurrent = ~1GB total, well within headroom.
- **test-runner max concurrent validators:** 1
- **Rationale:** Engine validation compiles native binaries to shared output paths and WASM tests rebuild/read shared `public/engine/` artifacts. Running test-runner validators in parallel would create avoidable write contention and noisy timing results.

## Engine Validation Setup
- No long-lived service is required for milestone `engine` user testing.
- Native engine coverage comes from `gcc -O2 -o /tmp/claudefish_test engine/src/*.c engine/tests/*.c -lm && /tmp/claudefish_test`.
- WASM/API coverage comes from `pnpm test`, which triggers `pretest` to build `public/engine/engine.js` and `public/engine/engine.wasm` if missing.
- The native engine runner prints suite-level PASS lines (`perft`, `legality`, `board representation`, `search`) rather than assertion IDs, so assertion mapping requires checking the corresponding files in `engine/tests/`.
- WASM/API assertion coverage lives in `tests/check-wasm-artifacts.test.mjs`; use that file to map `pnpm test` results back to contract assertions.
- For milestone `engine`, prefer `validation-contract.md` plus `features.json` `fulfills` as the source of truth for assertion IDs; `contract-work/wasm.md` descriptions do not perfectly align with the final validation contract numbering.
- Repository root is the isolation boundary; validators must not change source files and may only write reports/evidence in assigned `.factory/validation/...` and mission evidence paths.

## Website Validation Setup
- Build the WASM bundle before browser testing with `source ./emsdk/emsdk_env.sh && ./scripts/build-engine.sh`.
- Start the app with `pnpm dev --turbopack --port 3001`.
- Wait for `curl -sf http://localhost:3001` to succeed before launching browser validators.
- Website validators should use isolated browser sessions against the shared local app; the application keeps game state in browser memory, so separate sessions avoid cross-test interference.
- The public website surface currently exposes theme controls, difficulty selection, Undo, and New Game, but no board editor or FEN loader. Promotion and en-passant assertions may therefore remain blocked unless live engine play naturally reaches those prerequisites.
- Repository root remains the isolation boundary; validators must only write reports/evidence in assigned `.factory/validation/...` and mission evidence paths.

## Flow Validator Guidance: test-runner
- Stay inside the repository working tree and your assigned evidence/report paths.
- Do not run test-runner validators concurrently with other test-runner validators for this repo.
- Prefer existing project commands over ad hoc scripts:
  - Native engine assertions: `gcc -O2 -o /tmp/claudefish_test engine/src/*.c engine/tests/*.c -lm && /tmp/claudefish_test`
  - WASM assertions: `pnpm test`
- Capture concrete evidence from command output and map it back to assigned assertion IDs only.
- If a command fails before reaching assertion coverage, report affected assertions as `blocked` with the exact failing prerequisite.

## Flow Validator Guidance: agent-browser
- Use a unique browser session per validator group and do not reuse another validator's session.
- Stay within the shared website surface at `http://localhost:3001`; do not touch unrelated local services or ports.
- Treat each browser session as its own isolation boundary. Do not depend on persisted local storage, cookies, or prior game state from other validators.
- Global app resources are limited to the shared Next.js dev server and local CPU for engine search. Avoid stress loops or repeated maximum-depth searches outside the assigned assertions.
- Capture concrete browser evidence: key screenshots, visible UI states, timings when relevant, and any console/runtime errors tied to assigned assertions.
- If a prerequisite flow fails (for example page load or engine initialization), mark downstream assertions as `blocked` and name the failing prerequisite explicitly.
