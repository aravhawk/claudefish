#include "see.h"

#include "movorder.h"

static const int see_piece_values[PIECE_TYPE_NB] = {
    100,   /* PAWN   */
    320,   /* KNIGHT */
    330,   /* BISHOP */
    500,   /* ROOK   */
    950,   /* QUEEN  */
    20000  /* KING   */
};

static int see_piece_value(PieceType type) {
    return type >= 0 && type < PIECE_TYPE_NB ? see_piece_values[type] : 0;
}

/* Compute all attacks to a square using modified occupancy (for X-ray attacks) */
static Bitboard see_attacks_to(
    const Position *pos,
    int square,
    Bitboard occupancy
) {
    Bitboard attackers = 0;

    movegen_init();

    attackers |= movegen_pawn_attacks[WHITE][square] &
        pos->piece_bitboards[piece_bitboard_index(B_PAWN)];
    attackers |= movegen_pawn_attacks[BLACK][square] &
        pos->piece_bitboards[piece_bitboard_index(W_PAWN)];

    attackers |= movegen_knight_attacks[square] &
        (pos->piece_bitboards[piece_bitboard_index(W_KNIGHT)] |
         pos->piece_bitboards[piece_bitboard_index(B_KNIGHT)]);

    attackers |= movegen_king_attacks[square] &
        (pos->piece_bitboards[piece_bitboard_index(W_KING)] |
         pos->piece_bitboards[piece_bitboard_index(B_KING)]);

    attackers |= movegen_bishop_attacks(square, occupancy) &
        (pos->piece_bitboards[piece_bitboard_index(W_BISHOP)] |
         pos->piece_bitboards[piece_bitboard_index(B_BISHOP)] |
         pos->piece_bitboards[piece_bitboard_index(W_QUEEN)] |
         pos->piece_bitboards[piece_bitboard_index(B_QUEEN)]);

    attackers |= movegen_rook_attacks(square, occupancy) &
        (pos->piece_bitboards[piece_bitboard_index(W_ROOK)] |
         pos->piece_bitboards[piece_bitboard_index(B_ROOK)] |
         pos->piece_bitboards[piece_bitboard_index(W_QUEEN)] |
         pos->piece_bitboards[piece_bitboard_index(B_QUEEN)]);

    return attackers;
}

int see_evaluate(const Position *pos, Move move) {
    int target = move_target(move);
    int source = move_source(move);
    Bitboard occupancy;
    Bitboard attackers;
    Bitboard removed;
    int gain[32];
    int depth = 0;
    Color side;
    PieceType next_victim;

    if (pos == NULL) {
        return 0;
    }

    if (!movorder_is_capture(move)) {
        return 0;
    }

    occupancy = pos->occupancy[BOTH];

    /* For the initial capture, remove the moving piece from source and
       place it conceptually on the target (the captured piece is removed).
       For en passant, also remove the captured pawn. */
    occupancy &= ~BITBOARD_FROM_SQUARE(source);

    if ((move_flags(move) & MOVE_FLAG_EN_PASSANT) != 0) {
        int ep_captured_sq = target + (pos->side_to_move == WHITE ? -8 : 8);
        occupancy &= ~BITBOARD_FROM_SQUARE(ep_captured_sq);
        next_victim = PAWN;
    } else {
        next_victim = piece_type(move_captured_piece(move));
    }

    if (next_victim < 0 || next_victim >= PIECE_TYPE_NB) {
        next_victim = PAWN;
    }

    /* The side that just captured now has a piece on the target square.
       The opponent (side) gets to recapture. */
    side = (Color) (pos->side_to_move ^ 1);
    removed = BITBOARD_FROM_SQUARE(source);
    if ((move_flags(move) & MOVE_FLAG_EN_PASSANT) != 0) {
        int ep_captured_sq = target + (pos->side_to_move == WHITE ? -8 : 8);
        removed |= BITBOARD_FROM_SQUARE(ep_captured_sq);
    }

    /* Initial gain: value of the captured piece */
    gain[0] = see_piece_value(next_victim);

    /* The piece that just captured is now sitting on target */
    next_victim = piece_type(position_get_piece(pos, source));
    if (next_victim < 0 || next_victim >= PIECE_TYPE_NB) {
        next_victim = PAWN;
    }

    attackers = see_attacks_to(pos, target, occupancy) & ~removed;

    while (depth < 31) {
        int attacker_square = NO_SQUARE;
        int attacker_type = -1;
        Bitboard own_attackers;
        int pt;

        ++depth;

        /* Find the least valuable attacker for the current side */
        own_attackers = attackers & pos->occupancy[side] & ~removed;

        if (own_attackers == 0) {
            break;
        }

        /* Scan from pawn to king to find cheapest attacker */
        for (pt = PAWN; pt <= KING; ++pt) {
            Bitboard type_att = own_attackers &
                pos->piece_bitboards[piece_bitboard_index(make_piece(side, (PieceType) pt))];
            if (type_att != 0) {
                attacker_square = bitboard_lsb(type_att);
                attacker_type = pt;
                break;
            }
        }

        if (attacker_type < 0) {
            break;
        }

        /* This side captures the piece on the target square */
        gain[depth] = see_piece_value(next_victim) - gain[depth - 1];

        /* Pruning: if even winning the piece back can't make this exchange profitable,
           the opponent won't recapture (or we can stop early) */
        if (gain[depth - 1] >= 0 && gain[depth] < 0) {
            if (see_piece_value((PieceType) attacker_type) + gain[depth] < 0) {
                /* Even if we leave our attacker undefended, the exchange is losing.
                   This means the opponent will stop here, so we can too. */
                break;
            }
        }

        /* Remove the attacker from the board (it's now on the target square) */
        removed |= BITBOARD_FROM_SQUARE(attacker_square);
        occupancy &= ~BITBOARD_FROM_SQUARE(attacker_square);

        /* Recompute attackers for X-ray attacks through the removed square */
        attackers = see_attacks_to(pos, target, occupancy) & ~removed;

        next_victim = (PieceType) attacker_type;
        side = side ^ 1;
    }

    /* Negamax the gain list */
    while (--depth > 0) {
        if (gain[depth] > -gain[depth - 1]) {
            gain[depth - 1] = -gain[depth];
        }
    }

    return gain[0];
}

bool see_is_capture_bad(const Position *pos, Move move, int threshold) {
    return see_evaluate(pos, move) < threshold;
}
