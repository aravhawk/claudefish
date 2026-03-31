# Engine book + API notes

- `engine/src/book.c` loads `engine/book/rodent.bin` on native builds and falls back to the tiny embedded Polyglot blob in `engine/src/book_embedded.h` if the file is unavailable; the current `__EMSCRIPTEN__` path uses the embedded fallback directly, so the WASM build does not currently consume `rodent.bin`.
- `engine/src/engine.c` exposes the WASM-facing API: `init_engine`, `set_position`, `search_best_move`, `evaluate_position`, and `get_legal_moves`.
- `Position` keeps `history_hashes/history_count`, so repetition detection works for played move sequences and search lines inside one live engine position. The public `set_position(fen)` API reparses into a fresh `Position`, so a FEN-only front-end flow does not preserve threefold-repetition state unless it also replays move history or the API is extended.
