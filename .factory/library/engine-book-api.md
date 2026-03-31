# Engine book + API notes

- `engine/src/book.c` loads `engine/book/rodent.bin` first and falls back to the tiny embedded Polyglot blob in `engine/src/book_embedded.h` if the file is unavailable.
- `engine/src/engine.c` exposes the WASM-facing API: `init_engine`, `set_position`, `search_best_move`, `evaluate_position`, and `get_legal_moves`.
- `Position` now keeps `history_hashes/history_count`, so repetition detection works for played move sequences and search lines without additional front-end state.
