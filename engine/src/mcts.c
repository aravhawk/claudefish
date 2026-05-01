#include "mcts.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "evaluate.h"
#include "movegen.h"
#include "policy.h"
#include "position.h"

static bool mcts_initialized = false;

void mcts_init(void) {
    mcts_initialized = true;
}

static double mcts_value_to_centipawns(double value) {
    /* Map value from [-1, 1] to centipawns using a logistic-like transform */
    if (value > 0.99) return 30000.0;
    if (value < -0.99) return -30000.0;
    return value * 1000.0; /* simple linear mapping, ~1000cp per unit value */
}

static double mcts_centipawns_to_value(int cp) {
    if (cp > 30000) return 1.0;
    if (cp < -30000) return -1.0;
    return (double)cp / 1000.0;
}

static int mcts_find_king_square(const Position *pos, Color side) {
    int sq;
    Piece king = make_piece(side, KING);
    for (sq = 0; sq < BOARD_SQUARES; ++sq) {
        if (pos->mailbox[sq] == (uint8_t)king) return sq;
    }
    return -1;
}

static double mcts_evaluate_leaf(Position *pos, NNUEState *nnue_state, Color eval_for) {
    int eval_cp;
    double value;

    /* Use NNUE eval if available, otherwise classical */
    if (nnue_is_loaded() && nnue_state != NULL && nnue_state->initialized) {
        eval_cp = nnue_evaluate(nnue_state, pos);
    } else {
        eval_cp = eval_evaluate(pos);
    }

    value = mcts_centipawns_to_value(eval_cp);

    /* Flip if evaluating from opponent's perspective */
    if (eval_for != (Color)pos->side_to_move) {
        value = -value;
    }

    return value;
}

static int mcts_expand_node(
    MCTSContext *ctx,
    int node_idx,
    Position *pos
) {
    MoveList legal_moves;
    MCTSNode *node;
    int child_start;
    size_t i;

    if (node_idx < 0 || node_idx >= MCTS_MAX_NODES) return 0;
    node = &ctx->nodes[node_idx];

    if (node->expanded) return node->num_children;

    /* Check for terminal position */
    movegen_generate_legal(pos, &legal_moves);
    if (legal_moves.count == 0) {
        node->terminal = true;
        node->expanded = true;
        node->num_children = 0;
        return 0;
    }

    /* Allocate children */
    child_start = ctx->num_nodes;
    if (child_start + (int)legal_moves.count > MCTS_MAX_NODES) {
        /* Not enough node slots, limit children */
        if (child_start >= MCTS_MAX_NODES) return 0;
        legal_moves.count = (size_t)(MCTS_MAX_NODES - child_start);
    }

    node->children_start = (int16_t)child_start;
    node->num_children = (int)legal_moves.count;
    node->expanded = true;

    /* Get policy priors if available */
    {
        NNUEAccumulator *acc = NULL;
        double total_prior = 0.0;
        double priors[MOVEGEN_MAX_MOVES];

        if (nnue_is_loaded() && ctx->nnue_state.initialized) {
            acc = &ctx->nnue_state.acc_stack[ctx->nnue_state.current];
        }

        for (i = 0; i < legal_moves.count; ++i) {
            int policy_score = 0;
            if (acc != NULL && policy_is_loaded()) {
                policy_score = policy_score_move(acc, legal_moves.moves[i]);
            }

            /* Convert policy score to prior probability */
            priors[i] = exp((double)policy_score / 20000.0);
            total_prior += priors[i];
        }

        /* Create child nodes */
        for (i = 0; i < (size_t)node->num_children; ++i) {
            int child_idx = child_start + (int)i;
            if (child_idx >= MCTS_MAX_NODES) break;

            ctx->nodes[child_idx].move = legal_moves.moves[i];
            ctx->nodes[child_idx].visit_count = 0;
            ctx->nodes[child_idx].value_sum = 0.0;
            ctx->nodes[child_idx].prior = total_prior > 0.0 ? priors[i] / total_prior : 1.0 / legal_moves.count;
            ctx->nodes[child_idx].expanded = false;
            ctx->nodes[child_idx].terminal = false;
            ctx->nodes[child_idx].num_children = 0;
            ctx->nodes[child_idx].children_start = 0;
        }

        ctx->num_nodes = child_start + (int)legal_moves.count;
    }

    return node->num_children;
}

static int mcts_select_child(MCTSContext *ctx, int node_idx) {
    MCTSNode *node;
    int best_child = -1;
    double best_score = -1e18;
    int i;

    if (node_idx < 0) return -1;
    node = &ctx->nodes[node_idx];

    for (i = 0; i < node->num_children; ++i) {
        int child_idx = node->children_start + i;
        MCTSNode *child;
        double q, u, score;

        if (child_idx >= MCTS_MAX_NODES) continue;
        child = &ctx->nodes[child_idx];

        /* PUCT formula: Q(s,a) + c_puct * P(s,a) * sqrt(N_parent) / (1 + N(s,a)) */
        if (child->visit_count > 0) {
            q = child->value_sum / (double)child->visit_count;
        } else {
            q = 0.0; /* default value for unvisited nodes */
        }

        u = ((double)MCTS_CPUCT_NUMERATOR / MCTS_CPUCT_DENOMINATOR) * child->prior *
            sqrt((double)node->visit_count) / (1.0 + (double)child->visit_count);

        score = q + u;

        if (score > best_score) {
            best_score = score;
            best_child = child_idx;
        }
    }

    return best_child;
}

MCTSResult mcts_verify(
    Position *pos,
    NNUEState *parent_nnue_state,
    int simulations,
    Move ab_best_move,
    int ab_eval
) {
    MCTSContext ctx;
    MCTSResult result;
    int sim;
    Color root_player;

    if (!mcts_initialized || pos == NULL) {
        memset(&result, 0, sizeof(result));
        return result;
    }

    if (simulations <= 0) simulations = MCTS_DEFAULT_SIMULATIONS;
    if (simulations > MCTS_MAX_NODES) simulations = MCTS_MAX_NODES;

    memset(&ctx, 0, sizeof(ctx));
    ctx.root_player = (Color)pos->side_to_move;

    /* Copy NNUE state from parent search */
    if (parent_nnue_state != NULL && parent_nnue_state->initialized) {
        ctx.nnue_state = *parent_nnue_state;
    } else {
        nnue_state_init(&ctx.nnue_state, pos);
    }

    /* Initialize root node */
    ctx.nodes[0].move = 0;
    ctx.nodes[0].visit_count = 0;
    ctx.nodes[0].value_sum = 0.0;
    ctx.nodes[0].prior = 1.0;
    ctx.nodes[0].expanded = false;
    ctx.nodes[0].terminal = false;
    ctx.nodes[0].num_children = 0;
    ctx.nodes[0].children_start = 0;
    ctx.num_nodes = 1;
    root_player = ctx.root_player;

    /* MCTS main loop */
    for (sim = 0; sim < simulations; ++sim) {
        Position sim_pos = *pos;
        NNUEState sim_nnue = ctx.nnue_state;
        int path[MCTS_SIMULATION_DEPTH + 8];
        int path_len = 0;
        int current = 0; /* start at root */

        /* Selection: traverse tree using PUCT until we find an unexpanded node */
        while (ctx.nodes[current].expanded && !ctx.nodes[current].terminal && path_len < MCTS_SIMULATION_DEPTH) {
            int child = mcts_select_child(&ctx, current);
            if (child < 0) break;

            path[path_len++] = child;

            /* Make the move */
            if (movegen_make_move(&sim_pos, ctx.nodes[child].move)) {
                nnue_push(&sim_nnue);
                nnue_update_accumulator_incremental(&sim_nnue, &sim_pos,
                    position_get_piece(pos, move_source(ctx.nodes[child].move)),
                    move_source(ctx.nodes[child].move),
                    move_target(ctx.nodes[child].move));
            } else {
                break;
            }

            current = child;
        }

        /* Expansion: expand the leaf node */
        if (!ctx.nodes[current].terminal && path_len < MCTS_SIMULATION_DEPTH) {
            mcts_expand_node(&ctx, current, &sim_pos);
        }

        /* Evaluation: evaluate the leaf position */
        {
            double value;
            if (ctx.nodes[current].terminal) {
                /* Terminal: determine result */
                MoveList legal;
                movegen_generate_legal(&sim_pos, &legal);
                if (legal.count == 0 && movegen_is_in_check(&sim_pos, (Color)sim_pos.side_to_move)) {
                    /* Checkmate - bad for the side to move */
                    value = -1.0;
                } else {
                    /* Stalemate */
                    value = 0.0;
                }
            } else {
                value = mcts_evaluate_leaf(&sim_pos, &sim_nnue, root_player);
            }

            /* Backpropagation */
            {
                int i;
                for (i = path_len - 1; i >= 0; --i) {
                    int node_idx = path[i];
                    ctx.nodes[node_idx].visit_count++;
                    /* Alternate sign for each ply from root */
                    if ((path_len - i) % 2 == 1) {
                        ctx.nodes[node_idx].value_sum += value;
                    } else {
                        ctx.nodes[node_idx].value_sum -= value;
                    }
                }
                ctx.nodes[0].visit_count++;
                ctx.nodes[0].value_sum += value;
            }
        }

        /* Unmake all moves to restore position */
        {
            int i;
            for (i = path_len - 1; i >= 0; --i) {
                movegen_unmake_move(&sim_pos);
                nnue_pop(&sim_nnue);
            }
        }
    }

    /* Select best move from root by visit count */
    {
        int best_child = -1;
        int best_visits = -1;
        double best_value = -1e18;
        int i;

        for (i = 0; i < ctx.nodes[0].num_children; ++i) {
            int child_idx = ctx.nodes[0].children_start + i;
            MCTSNode *child;

            if (child_idx >= MCTS_MAX_NODES) continue;
            child = &ctx.nodes[child_idx];

            if (child->visit_count > best_visits ||
                (child->visit_count == best_visits &&
                 (child->visit_count > 0 ? child->value_sum / child->visit_count : 0) > best_value)) {
                best_visits = child->visit_count;
                best_value = child->visit_count > 0 ? child->value_sum / child->visit_count : 0;
                best_child = child_idx;
            }
        }

        if (best_child >= 0) {
            result.best_move = ctx.nodes[best_child].move;
            result.best_value = best_value;
        } else {
            result.best_move = 0;
            result.best_value = 0.0;
        }
    }

    result.simulations_run = simulations;
    result.mcts_eval = (int)mcts_value_to_centipawns(result.best_value);

    /* Check disagreement with alpha-beta */
    result.disagrees = false;
    if (ab_best_move != 0 && result.best_move != 0 && result.best_move != ab_best_move) {
        /* MCTS chose a different move */
        int eval_diff = abs(result.mcts_eval - ab_eval);
        if (eval_diff > 50) {
            result.disagrees = true;
        }
    }

    return result;
}

bool mcts_is_critical_position(
    const Position *pos,
    int best_move_eval,
    int second_move_eval,
    int depth
) {
    int eval_swing;
    bool in_check;

    if (pos == NULL) return false;

    /* Always verify at high depth when there's a significant eval difference */
    eval_swing = abs(best_move_eval - second_move_eval);
    if (eval_swing > MCTS_EVAL_THRESHOLD && depth >= 6) {
        return true;
    }

    /* In-check positions are more likely to be tactical */
    in_check = movegen_is_in_check(pos, (Color)pos->side_to_move);
    if (in_check && depth >= 4) {
        return true;
    }

    return false;
}

void mcts_cleanup(void) {
    mcts_initialized = false;
}
