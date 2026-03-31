#include "engine.h"

#include <string.h>

#include "book.h"
#include "draw.h"
#include "evaluate.h"
#include "search.h"
#include "time.h"

enum {
    ENGINE_LEGAL_MOVES_BUFFER_SIZE = (MOVEGEN_MAX_MOVES * 6) + MOVEGEN_MAX_MOVES + 1
};

static const char *engine_start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
static Position engine_position;
static bool engine_ready = false;
static char engine_best_move_buffer[6];
static char engine_legal_moves_buffer[ENGINE_LEGAL_MOVES_BUFFER_SIZE];

static void engine_set_empty_string(char *buffer, size_t size) {
    if (buffer != NULL && size > 0) {
        buffer[0] = '\0';
    }
}

static Move engine_first_legal_move(Position *pos) {
    MoveList legal_moves;

    if (pos == NULL) {
        return 0;
    }

    movegen_generate_legal(pos, &legal_moves);
    return legal_moves.count > 0 ? legal_moves.moves[0] : 0;
}

int init_engine(void) {
    movegen_init();
    eval_init();
    search_init();
    book_init();

    if (!position_from_fen(&engine_position, engine_start_fen)) {
        engine_ready = false;
        return -1;
    }

    engine_set_empty_string(engine_best_move_buffer, sizeof(engine_best_move_buffer));
    engine_set_empty_string(engine_legal_moves_buffer, sizeof(engine_legal_moves_buffer));
    engine_ready = true;
    return 0;
}

int set_position(const char *fen) {
    Position next_position;

    if ((!engine_ready && init_engine() != 0) || fen == NULL) {
        return -1;
    }

    if (!position_from_fen(&next_position, fen)) {
        return -1;
    }

    engine_position = next_position;
    return 0;
}

const char *search_best_move(int depth, int time_ms) {
    SearchResult result;
    TimeControl time_control;
    Move move = 0;
    int allocation_ms;

    if (!engine_ready && init_engine() != 0) {
        return "";
    }

    engine_set_empty_string(engine_best_move_buffer, sizeof(engine_best_move_buffer));

    if (book_probe_move(&engine_position, &move) && search_move_to_uci(move, engine_best_move_buffer)) {
        return engine_best_move_buffer;
    }

    if (depth <= 0 || depth > SEARCH_MAX_DEPTH) {
        depth = SEARCH_MAX_DEPTH;
    }

    time_control.remaining_ms = 0;
    time_control.increment_ms = 0;
    time_control.move_time_ms = time_ms;
    time_control.moves_to_go = 0;
    allocation_ms = time_ms > 0 ? time_calculate_allocation_ms(&time_control) : 0;

    if (!search_iterative_deepening(&engine_position, depth, allocation_ms, &result) || result.best_move == 0) {
        move = engine_first_legal_move(&engine_position);
    } else {
        move = result.best_move;
    }

    if (move == 0 || !search_move_to_uci(move, engine_best_move_buffer)) {
        engine_set_empty_string(engine_best_move_buffer, sizeof(engine_best_move_buffer));
    }

    return engine_best_move_buffer;
}

int evaluate_position(void) {
    if (!engine_ready && init_engine() != 0) {
        return 0;
    }

    if (draw_is_draw(&engine_position)) {
        return draw_score(&engine_position);
    }

    return eval_evaluate(&engine_position);
}

const char *get_legal_moves(void) {
    MoveList legal_moves;
    size_t index;
    size_t offset = 0;

    if (!engine_ready && init_engine() != 0) {
        return "";
    }

    engine_set_empty_string(engine_legal_moves_buffer, sizeof(engine_legal_moves_buffer));
    movegen_generate_legal(&engine_position, &legal_moves);

    for (index = 0; index < legal_moves.count; ++index) {
        char move_uci[6];
        size_t move_length;

        if (!search_move_to_uci(legal_moves.moves[index], move_uci)) {
            continue;
        }

        move_length = strlen(move_uci);
        if (offset + move_length + 2 >= sizeof(engine_legal_moves_buffer)) {
            break;
        }

        if (index > 0) {
            engine_legal_moves_buffer[offset++] = ',';
        }

        memcpy(engine_legal_moves_buffer + offset, move_uci, move_length);
        offset += move_length;
        engine_legal_moves_buffer[offset] = '\0';
    }

    return engine_legal_moves_buffer;
}
