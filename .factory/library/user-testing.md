# User Testing

## Validation Surface
- **Engine (Milestone 1):** Automated tests via native C compilation (gcc) + WASM integration tests (Node.js)
- **Website (Milestone 2):** agent-browser at http://localhost:3000
- **Tool:** agent-browser for all browser-based UI testing

## Validation Concurrency
- **Max concurrent validators:** 2
- **Rationale:** 18GB RAM, ~10GB used at baseline. Each agent-browser instance ~300MB + Next.js dev server ~400MB. 2 concurrent = ~1GB total, well within headroom.
- **Engine tests:** single-threaded, no concurrency concerns
