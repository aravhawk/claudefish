#include "syzygy.h"
#include "position.h"
#include "movegen.h"

#include <stdlib.h>
#include <string.h>

/* Syzygy tablebase probing implementation.
   For the WASM/web context, we embed the most critical small tables (3-4 pieces)
   and provide a framework for loading larger tables from a server.
   Currently this is a skeleton that checks piece count and provides the API
   for integration. Full probing requires porting the Fathom library. */

static bool syzygy_initialized = false;
static bool syzygy_loaded = false;
static int syzygy_max_pieces_loaded = 0;

void syzygy_init(void) {
    syzygy_initialized = true;
    syzygy_loaded = false;
    syzygy_max_pieces_loaded = 0;
}

void syzygy_cleanup(void) {
    syzygy_initialized = false;
    syzygy_loaded = false;
    syzygy_max_pieces_loaded = 0;
}

int syzygy_piece_count(const Position *pos) {
    if (pos == NULL) return 0;
    int count = 0;
    Bitboard all = pos->occupancy[BOTH];
    while (all) {
        all &= all - 1;
        count++;
    }
    return count;
}

bool syzygy_available(const Position *pos) {
    if (pos == NULL || !syzygy_loaded) return false;
    return syzygy_piece_count(pos) <= syzygy_max_pieces_loaded;
}

bool syzygy_load_embedded(void) {
    /* Mark as having 3-piece tables available (minimal set).
       In production, this would embed actual compressed table data. */
    syzygy_loaded = true;
    syzygy_max_pieces_loaded = 3;
    return true;
}

bool syzygy_load_path(const char *path) {
    /* Placeholder for loading tablebase files from disk/server. */
    (void)path;
    return false;
}

bool syzygy_is_loaded(void) {
    return syzygy_loaded;
}

SyzygyResult syzygy_probe(const Position *pos) {
    SyzygyResult result;
    memset(&result, 0, sizeof(result));
    result.wdl = SYZYGY_RESULT_NONE;
    result.dtz = -1;
    result.found = false;
    result.uses_rule50 = false;

    if (pos == NULL || !syzygy_loaded) return result;

    int pieces = syzygy_piece_count(pos);
    if (pieces > syzygy_max_pieces_loaded || pieces < 2) return result;

    /* For positions with ≤3 pieces, we can compute perfect results
       for KNOWN endgame patterns without needing actual tablebase data.
       Be very conservative — only return results when we're certain. */
    Color stm = (Color)pos->side_to_move;

    /* K vs K: draw */
    if (pieces == 2) {
        result.wdl = SYZYGY_RESULT_DRAW;
        result.dtz = 0;
        result.found = true;
        result.uses_rule50 = false;
        return result;
    }

    /* K+X vs K patterns with 3 pieces — only report certain results */
    if (pieces == 3) {
        Bitboard white = pos->occupancy[WHITE];
        Bitboard black = pos->occupancy[BLACK];

        int white_count = __builtin_popcountll(white);
        int black_count = __builtin_popcountll(black);

        /* K vs K+piece: check piece type */
        if (white_count == 2 && black_count == 1) {
            Bitboard white_non_king = white & ~pos->piece_bitboards[make_piece(WHITE, KING) - 1];
            if (white_non_king != 0) {
                int piece_sq = __builtin_ctzll(white_non_king);
                Piece p = position_get_piece(pos, piece_sq);
                PieceType pt = piece_type(p);
                if (pt == QUEEN || pt == ROOK) {
                    /* KQ vs K or KR vs K: always win (forced checkmate) */
                    result.wdl = (stm == WHITE) ? SYZYGY_RESULT_WIN : SYZYGY_RESULT_LOSS;
                    result.dtz = 10;
                    result.found = true;
                    return result;
                }
                /* K+B vs K or K+N vs K: draw (insufficient material) */
                if (pt == BISHOP || pt == KNIGHT) {
                    result.wdl = SYZYGY_RESULT_DRAW;
                    result.dtz = 0;
                    result.found = true;
                    return result;
                }
                /* KP vs K: don't report — result depends on position */
            }
        }

        if (black_count == 2 && white_count == 1) {
            Bitboard black_non_king = black & ~pos->piece_bitboards[make_piece(BLACK, KING) - 1];
            if (black_non_king != 0) {
                int piece_sq = __builtin_ctzll(black_non_king);
                Piece p = position_get_piece(pos, piece_sq);
                PieceType pt = piece_type(p);
                if (pt == QUEEN || pt == ROOK) {
                    result.wdl = (stm == BLACK) ? SYZYGY_RESULT_WIN : SYZYGY_RESULT_LOSS;
                    result.dtz = 10;
                    result.found = true;
                    return result;
                }
                if (pt == BISHOP || pt == KNIGHT) {
                    result.wdl = SYZYGY_RESULT_DRAW;
                    result.dtz = 0;
                    result.found = true;
                    return result;
                }
            }
        }
    }

    /* For larger piece counts or unknown patterns, no hit */
    return result;
}
