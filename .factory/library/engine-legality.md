# Engine legality status

- `engine/src/movegen.c` now provides `movegen_generate_legal`, `movegen_make_move`, `movegen_unmake_move`, attack detection, pin detection, and mate/stalemate helpers.
- Castling generation already enforces: rights present, rook/king on start squares, empty path, king not in check, and transit/destination squares not attacked.
- En passant is generated pseudo-legally when `en_passant_sq` is set; legality filtering removes pinned/discovered-check cases through make/unmake + king safety checks.
- `Position` now carries a `state_stack` used by make/unmake to restore castling rights, en passant square, clocks, Zobrist hashes, and the incremental evaluation caches (`pawn_hash`, `pst_mg`, `pst_eg`) exactly.
- `engine/tests/test_legality.c` covers castling, en passant, promotions, single/double check handling, pinned pieces, mate/stalemate, and recursive make/unmake integrity.
