#include "draw.h"

static int draw_piece_count(const Position *pos, Piece piece) {
    int index = piece_bitboard_index(piece);

    if (pos == NULL || index < 0) {
        return 0;
    }

    return bitboard_popcount(pos->piece_bitboards[index]);
}

bool draw_is_threefold_repetition(const Position *pos) {
    int current_index;
    int minimum_index;
    int index;
    int repetitions = 1;

    if (pos == NULL || pos->history_count < 3) {
        return false;
    }

    current_index = (int) pos->history_count - 1;
    minimum_index = current_index - (int) pos->halfmove_clock;
    if (minimum_index < 0) {
        minimum_index = 0;
    }

    for (index = current_index - 2; index >= minimum_index; index -= 2) {
        if (pos->history_hashes[index] == pos->zobrist_hash) {
            ++repetitions;
            if (repetitions >= 3) {
                return true;
            }
        }
    }

    return false;
}

bool draw_is_fifty_move_rule(const Position *pos) {
    return pos != NULL && pos->halfmove_clock >= 100;
}

bool draw_has_insufficient_material(const Position *pos) {
    int white_knights;
    int black_knights;
    int white_bishops;
    int black_bishops;
    int total_minors;

    if (pos == NULL) {
        return false;
    }

    if (draw_piece_count(pos, W_PAWN) != 0 || draw_piece_count(pos, B_PAWN) != 0 ||
        draw_piece_count(pos, W_ROOK) != 0 || draw_piece_count(pos, B_ROOK) != 0 ||
        draw_piece_count(pos, W_QUEEN) != 0 || draw_piece_count(pos, B_QUEEN) != 0) {
        return false;
    }

    white_knights = draw_piece_count(pos, W_KNIGHT);
    black_knights = draw_piece_count(pos, B_KNIGHT);
    white_bishops = draw_piece_count(pos, W_BISHOP);
    black_bishops = draw_piece_count(pos, B_BISHOP);
    total_minors = white_knights + black_knights + white_bishops + black_bishops;

    return total_minors <= 1;
}

bool draw_is_draw(const Position *pos) {
    return draw_is_fifty_move_rule(pos) ||
        draw_is_threefold_repetition(pos) ||
        draw_has_insufficient_material(pos);
}

int draw_score(const Position *pos) {
    (void) pos;
    return 0;
}
