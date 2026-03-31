# Claudefish Chess Engine — Architecture & Research Report

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Board Representation](#board-representation)
3. [Move Generation](#move-generation)
4. [Search Algorithm](#search-algorithm)
5. [Evaluation Function](#evaluation-function)
6. [Transposition Table](#transposition-table)
7. [Opening Book](#opening-book)
8. [Endgame Techniques](#endgame-techniques)
9. [Perft Testing](#perft-testing)
10. [Strength Targets & Key Techniques](#strength-targets)
11. [Implementation Roadmap](#implementation-roadmap)

---

## 1. Executive Summary

Claudefish is a classical chess engine written in C, targeting WebAssembly compilation. Without NNUE or neural network evaluation, a well-implemented classical engine can realistically achieve **2800–3200 ELO** on rating lists (CCRL). The strongest non-neural-network engines historically include Stockfish pre-NNUE (~3200 ELO), Laser (~3100), and Ethereal (~3100). A well-implemented HCE (hand-crafted evaluation) engine with modern search techniques is still remarkably strong.

**Key finding:** Search is more important than evaluation. A mediocre evaluation with excellent search will beat an excellent evaluation with mediocre search. The biggest ELO gains come from:
1. Move ordering (~200 ELO)
2. Null move pruning (~150 ELO)
3. Late move reductions (~200 ELO)
4. Transposition tables (~100-150 ELO)
5. Aspiration windows (~30-50 ELO)
6. Quiescence search (~300+ ELO — essential, not optional)

---

## 2. Board Representation

### Bitboard Architecture

Use **12 bitboards** (one per piece type per color) plus auxiliary bitboards:

```c
typedef uint64_t Bitboard;

typedef struct {
    Bitboard pieces[2][6];     // [color][piece_type]
    Bitboard occupancy[3];     // [WHITE, BLACK, BOTH]
    int squares[64];           // piece on each square (mailbox redundancy)
    int side_to_move;          // WHITE or BLACK
    int castling_rights;       // 4-bit KQkq
    int en_passant_sq;         // target square or NO_SQ
    int halfmove_clock;        // for 50-move rule
    int fullmove_number;
    uint64_t hash;             // Zobrist hash (incrementally updated)
    uint64_t pawn_hash;        // separate pawn structure hash
} Position;
```

### Why Bitboards?
- **Parallel operations**: A single `&`, `|`, or `^` operates on all 64 squares simultaneously
- **Move generation**: Sliding piece attacks computed via magic bitboards in O(1)
- **Evaluation**: Pawn structure, king safety zones, piece mobility — all expressible as bitboard operations
- **Perfect for C**: Native 64-bit integer operations map directly

### Piece Encoding

```c
enum Piece { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum Color { WHITE, BLACK, BOTH };
```

### Square Mapping

Use Little-Endian Rank-File (LERF) mapping:
- a1 = 0, b1 = 1, ..., h1 = 7
- a2 = 8, ..., h8 = 63

This means rank = square / 8, file = square % 8.

---

## 3. Move Generation

### Magic Bitboards (Critical for Performance)

Magic bitboards provide O(1) lookup for sliding piece (bishop, rook, queen) attack sets.

**Concept:**
1. For each square, compute a mask of relevant blocker squares (exclude edge squares)
2. Given an occupancy bitboard, multiply by a "magic number" and shift right to get an index
3. Use that index into a precomputed attack table

```c
// Rook attack lookup
static inline Bitboard rook_attacks(int square, Bitboard occupancy) {
    occupancy &= rook_masks[square];
    occupancy *= rook_magics[square];
    occupancy >>= (64 - rook_shift[square]);
    return rook_table[square][occupancy];
}

// Bishop attack lookup
static inline Bitboard bishop_attacks(int square, Bitboard occupancy) {
    occupancy &= bishop_masks[square];
    occupancy *= bishop_magics[square];
    occupancy >>= (64 - bishop_shift[square]);
    return bishop_table[square][occupancy];
}

// Queen = rook | bishop
static inline Bitboard queen_attacks(int square, Bitboard occupancy) {
    return rook_attacks(square, occupancy) | bishop_attacks(square, occupancy);
}
```

**Finding Magic Numbers:**
- Use trial and random generation: generate random sparse numbers, test if they produce a valid mapping
- Known good magics exist — can be hardcoded from published sources (e.g., chessprogramming wiki)
- "Fancy" magic bitboards use variable-size tables; "plain" magics use fixed-size tables

**Memory requirements:**
- Rook: 64 squares × up to 4096 entries = ~800 KB
- Bishop: 64 squares × up to 512 entries = ~40 KB
- Total: ~840 KB (fits well in WebAssembly memory)

### Non-Sliding Pieces

Knight and king attacks: precomputed 64-entry lookup tables.

```c
Bitboard knight_attacks[64];  // precomputed
Bitboard king_attacks[64];    // precomputed
```

### Pawn Moves

Pawn moves are the most complex due to: single push, double push, captures (left/right), en passant, promotions (4 piece types).

```c
Bitboard pawn_attacks[2][64];  // [color][square], precomputed
```

### Move Encoding

Pack move data into a 32-bit integer for cache efficiency:

```c
// Move encoding (32-bit):
// bits 0-5:   source square (0-63)
// bits 6-11:  target square (0-63)
// bits 12-15: flags (capture, promotion piece, castling, en passant, double push)
// bits 16-19: captured piece type (for undo)
// bits 20-23: promotion piece type
typedef uint32_t Move;
```

### Legal Move Generation Strategy

Two approaches:
1. **Pseudo-legal + legality check**: Generate all pseudo-legal moves, make each, check if king is in check, unmake if illegal. Simpler to implement.
2. **Strictly legal**: Only generate legal moves. More complex but avoids make/unmake overhead.

**Recommendation:** Start with pseudo-legal generation + legality check. It's simpler to debug and the performance difference is small with good move ordering (most illegal moves are never searched).

---

## 4. Search Algorithm

### Core: Negamax with Alpha-Beta Pruning

```
function negamax(position, depth, alpha, beta):
    if depth == 0:
        return quiescence_search(position, alpha, beta)

    for each move in ordered_moves(position):
        make_move(position, move)
        score = -negamax(position, depth - 1, -beta, -alpha)
        unmake_move(position, move)

        if score >= beta:
            return beta    // beta cutoff
        if score > alpha:
            alpha = score  // new best

    return alpha
```

### Principal Variation Search (PVS)

**ELO gain: ~30-50 ELO**

After searching the first (presumably best) move with a full window, search remaining moves with a null/zero window. Only re-search with full window if the null window search fails high.

```
function pvs(position, depth, alpha, beta):
    // ... TT probe, null move, etc.

    first_move = true
    for each move in ordered_moves(position):
        make_move(position, move)

        if first_move:
            score = -pvs(position, depth-1, -beta, -alpha)
            first_move = false
        else:
            // Null window search
            score = -pvs(position, depth-1, -alpha-1, -alpha)
            if score > alpha and score < beta:
                // Re-search with full window
                score = -pvs(position, depth-1, -beta, -alpha)

        unmake_move(position, move)
        // ... update alpha, beta cutoff
```

### Iterative Deepening

**Essential** — not just for time management, but also provides the PV for move ordering.

```
function iterative_deepening(position, time_limit):
    for depth = 1 to MAX_DEPTH:
        score = pvs(position, depth, -INFINITY, INFINITY)
        if time_exceeded(time_limit):
            break
    return best_move
```

### Aspiration Windows

**ELO gain: ~30-50 ELO**

Instead of searching with (-∞, +∞), use a narrow window around the previous iteration's score:

```
window = 50  // centipawns
alpha = prev_score - window
beta  = prev_score + window
score = pvs(position, depth, alpha, beta)

if score <= alpha or score >= beta:
    // Failed — re-search with full window
    score = pvs(position, depth, -INFINITY, INFINITY)
```

Use graduated widening: if the first window fails, try a wider window before falling back to full.

### Quiescence Search

**ELO gain: ~300+ ELO (absolutely essential)**

At leaf nodes, continue searching captures (and possibly checks) until a "quiet" position is reached:

```
function quiescence(position, alpha, beta):
    stand_pat = evaluate(position)

    if stand_pat >= beta:
        return beta
    if stand_pat > alpha:
        alpha = stand_pat

    for each capture in ordered_captures(position):
        make_move(position, capture)
        score = -quiescence(position, -beta, -alpha)
        unmake_move(position, capture)

        if score >= beta:
            return beta
        if score > alpha:
            alpha = score

    return alpha
```

**Important:** Use delta pruning in quiescence search — skip captures that can't possibly raise alpha even with the most optimistic material gain.

### Null Move Pruning

**ELO gain: ~100-150 ELO**

If the side to move can "pass" (null move) and still cause a beta cutoff at reduced depth, the position is likely so good that we can prune:

```
if depth >= 3 and not_in_check and has_non_pawn_material:
    make_null_move(position)
    score = -pvs(position, depth - R - 1, -beta, -beta + 1)
    unmake_null_move(position)

    if score >= beta:
        return beta  // null move cutoff
```

- R = 2 or 3 (reduction depth). Adaptive R based on depth is best (R=2 for shallow, R=3 for deep).
- **Never** do null move in check or in zugzwang-prone endgames (king + pawns only).

### Late Move Reductions (LMR)

**ELO gain: ~150-200 ELO**

Moves ordered late (unlikely to be good) are searched at reduced depth:

```
if move_count > 3 and depth >= 3 and not_tactical_move:
    reduction = lmr_table[depth][move_count]  // precomputed log formula
    score = -pvs(position, depth - 1 - reduction, -alpha - 1, -alpha)
    if score > alpha:
        score = -pvs(position, depth - 1, -beta, -alpha)  // re-search
```

LMR reduction formula (typical): `reduction = int(0.75 + log(depth) * log(move_count) / 2.25)`

### Late Move Pruning (LMP)

At shallow depths, simply skip late moves that are unlikely to be interesting:

```
if depth <= 3 and move_count > lmp_threshold[depth] and not_tactical:
    continue  // skip this move entirely
```

### Futility Pruning

**ELO gain: ~30-50 ELO**

At shallow depths, if static eval + a margin is still below alpha, skip non-tactical moves:

```
if depth <= 3 and not_in_check:
    futility_margin = depth * 150  // centipawns
    if static_eval + futility_margin <= alpha:
        // Only search captures and checks
```

### Razoring

At shallow depths, if static eval is far below alpha, drop into quiescence search:

```
if depth <= 3 and static_eval + razor_margin[depth] <= alpha:
    score = quiescence(position, alpha, beta)
    if score <= alpha:
        return alpha
```

### Check Extensions

Extend search by 1 ply when in check. This is essentially free (check positions have few legal moves) and avoids missing tactical shots.

### Singular Extensions

If the TT move is significantly better than all alternatives (at reduced depth), extend it by 1 ply. Expensive but strong (~10-20 ELO).

---

## 5. Evaluation Function

### Overview

A strong hand-crafted evaluation (HCE) uses **tapered evaluation** — interpolating between opening/middlegame and endgame scores based on remaining material:

```c
int evaluate(Position *pos) {
    int mg_score = 0;  // middlegame
    int eg_score = 0;  // endgame

    // ... accumulate all terms for both mg and eg ...

    int phase = calculate_phase(pos);  // 0 = endgame, 256 = opening
    return (mg_score * phase + eg_score * (256 - phase)) / 256;
}
```

### Material Values (Centipawns)

| Piece  | Middlegame | Endgame |
|--------|-----------|---------|
| Pawn   | 100       | 120     |
| Knight | 320       | 310     |
| Bishop | 330       | 320     |
| Rook   | 500       | 530     |
| Queen  | 950       | 1000    |

### Piece-Square Tables (PSTs)

**ELO gain: ~100-150 ELO** (one of the biggest single improvements)

Each piece has a 64-entry table of positional bonuses for middlegame and endgame:

```c
int pst_mg[6][64];  // [piece_type][square]
int pst_eg[6][64];
```

Key PST principles:
- **Pawns**: Encourage center control (d4/e4), penalize doubled/backward pawns
- **Knights**: Strong on outposts (d5/e5), weak on edges
- **Bishops**: Prefer long diagonals, penalize blocked positions
- **Rooks**: Bonus on open files, 7th rank
- **Queen**: Slight center preference but not too early development
- **King**: Middlegame: stay castled (g1/c1); Endgame: centralize (d4/e4)

PSTs should be incrementally updated on make/unmake for O(1) evaluation of this component.

### Pawn Structure

**ELO gain: ~50-80 ELO**

Evaluate with a **separate pawn hash table** (pawn structure changes infrequently):

- **Doubled pawns**: -15 to -25 cp per doubled pawn
- **Isolated pawns**: -15 to -20 cp (isolated d-pawn slightly less penalized)
- **Backward pawns**: -10 to -15 cp
- **Passed pawns**: +20 to +120 cp depending on rank (exponentially increasing)
  - Additional bonus if supported by another pawn
  - Additional bonus if path to promotion is clear
  - Endgame passed pawn bonus should be very large
- **Connected pawns**: +5 to +15 cp
- **Pawn islands**: -5 cp per island beyond 1
- **Pawn chain**: Bonus for connected pawn chains

### King Safety

**ELO gain: ~80-120 ELO** (one of the most important evaluation terms)

Key factors:
- **Pawn shield**: Bonus for pawns in front of castled king (f2/g2/h2 or f7/g7/h7)
  - Missing shield pawn: -30 to -50 cp per pawn
  - Advanced shield pawn: -15 to -25 cp
- **King attack zone**: Count attackers and attack weight
  - Each attacker type has a weight (Knight: 20, Bishop: 20, Rook: 40, Queen: 80)
  - Look up attack score from a table indexed by total weight
  - Scale by number of attackers (need ≥2 attackers for meaningful attack)
- **Open files near king**: -20 to -40 cp for semi-open/open files adjacent to king
- **King tropism**: Bonus for pieces near opponent's king (simple distance-based)

### Piece Mobility

**ELO gain: ~40-60 ELO**

Count legal/pseudo-legal moves for each piece (using attack bitboards):

```c
int knight_mobility_bonus[] = {-20, -10, 0, 5, 10, 15, 18, 20, 22};  // by # of moves
int bishop_mobility_bonus[] = {-25, -15, -5, 0, 5, 10, 15, 18, 20, 22, 24, 26, 27, 28};
int rook_mobility_bonus[]   = {-20, -12, -5, 0, 5, 8, 11, 14, 17, 20, 22, 24, 25, 26, 27};
int queen_mobility_bonus[]  = {-15, -10, -5, -2, 0, 2, 4, 6, 8, 10, 12, 14, 15, ...};
```

### Additional Evaluation Terms

Sorted by approximate ELO impact:

| Feature | Approx. ELO Gain | Complexity |
|---------|------------------|------------|
| Piece-Square Tables | 100-150 | Low |
| King Safety | 80-120 | High |
| Pawn Structure | 50-80 | Medium |
| Piece Mobility | 40-60 | Medium |
| Bishop Pair | 20-30 | Low |
| Rook on Open File | 15-25 | Low |
| Rook on 7th Rank | 10-15 | Low |
| Knight Outposts | 15-20 | Medium |
| Tempo Bonus | 10-15 | Low |
| Threats | 15-25 | Medium |
| Space Advantage | 10-15 | Medium |

**Bishop Pair**: +30-50 cp bonus when having both bishops (especially in open positions).

**Rook on Open File**: +20 cp for open file, +10 cp for semi-open file.

**Rook on 7th Rank**: +20-30 cp when opponent's pawns are still on their 2nd rank.

**Knight Outposts**: Bonus for knights on squares protected by own pawns and not attackable by opponent pawns.

**Tempo**: Small bonus (~10-15 cp) for the side to move.

---

## 6. Transposition Table

### Zobrist Hashing

Generate random 64-bit numbers for each (piece, square, color) combination, plus castling rights, en passant file, and side to move:

```c
uint64_t zobrist_pieces[2][6][64];   // [color][piece][square]
uint64_t zobrist_castling[16];       // 4-bit castling rights
uint64_t zobrist_en_passant[8];      // file of en passant square
uint64_t zobrist_side;               // side to move
```

Hash is **incrementally updated** on make/unmake:
```c
hash ^= zobrist_pieces[color][piece][from_sq];
hash ^= zobrist_pieces[color][piece][to_sq];
// ... XOR in/out captured pieces, castling changes, etc.
```

### Table Structure

```c
typedef struct {
    uint64_t key;       // Full Zobrist hash (or upper bits for verification)
    int16_t  score;     // Stored score
    int16_t  static_eval; // Static evaluation (for pruning)
    Move     best_move; // Best move found
    uint8_t  depth;     // Depth of search
    uint8_t  flag;      // EXACT, ALPHA (upper bound), BETA (lower bound)
    uint8_t  age;       // For replacement scheme
} TTEntry;
```

Entry size: 16 bytes (fits 2 entries per cache line on most architectures).

### Table Size

For WebAssembly, target **16-64 MB** transposition table:
- 16 MB = 1M entries (16 bytes each)
- 64 MB = 4M entries

Use power-of-2 sizing for fast modulo via bitmask: `index = hash & (table_size - 1)`

### Replacement Strategy

**Always-replace** is simplest and works well. Better: use **depth-preferred with aging**:
- Replace if: new entry has greater or equal depth, OR existing entry is from a previous search
- Store 2 entries per bucket: one depth-preferred, one always-replace

### TT Usage in Search

```c
// At start of search node:
TTEntry *entry = tt_probe(pos->hash);
if (entry && entry->depth >= depth) {
    if (entry->flag == EXACT) return entry->score;
    if (entry->flag == BETA && entry->score >= beta) return beta;
    if (entry->flag == ALPHA && entry->score <= alpha) return alpha;
}
// Use entry->best_move for move ordering even if depth is insufficient
```

---

## 7. Opening Book

### Recommended Format: Polyglot (.bin)

Polyglot is the de facto standard for chess engine opening books:

- **Binary format**: Fixed 16-byte entries, sorted by hash for binary search
- **Portable**: Works across platforms (important for WebAssembly)
- **Widely available**: Many pre-built books exist
- **Simple to implement**: ~100 lines of C code for reading

### Polyglot Entry Format (16 bytes)

```c
typedef struct {
    uint64_t key;       // Polyglot hash of position
    uint16_t move;      // Encoded move
    uint16_t weight;    // Relative weight for move selection
    uint32_t learn;     // Learning data (can be ignored)
} PolyglotEntry;
```

### Polyglot Hash

Polyglot uses its own Zobrist hash scheme (different from the engine's internal hash). The specification defines exact random numbers to use. Implementation requires:
1. Computing the Polyglot hash for the current position
2. Binary search in the sorted .bin file
3. Collecting all entries with matching hash
4. Selecting a move based on weights (weighted random or best weight)

### Move Encoding in Polyglot

```
bits 0-2:   to file (0=a, 7=h)
bits 3-5:   to rank (0=1, 7=8)
bits 6-8:   from file
bits 9-11:  from rank
bits 12-14: promotion piece (1=knight, 2=bishop, 3=rook, 4=queen)
```

Castling is encoded as king moving to the rook's square (e1-h1 for O-O, e1-a1 for O-O-O).

### Integration Strategy for WebAssembly

For an embedded/Wasm engine:
1. **Embed a compact book** (1-5 MB) as a binary blob compiled into the Wasm module
2. Use weighted random selection for variety
3. Fall out of book when no entry is found and switch to search

### Recommended Books

- **Performance.bin**: Popular, well-tested, ~10 MB
- **gm2001.bin**: Based on GM games, ~4 MB
- **Custom**: Can create from PGN databases using `polyglot make-book`

---

## 8. Endgame Techniques

### Material-Based Evaluation Adjustments

- **Insufficient material detection**: KvK, KNvK, KBvK → draw
- **KBN vs K**: Special evaluation to drive king to correct corner
- **Known drawn endgames**: KR vs KB, KR vs KN (mostly drawn with perfect play)

### Piece Value Adjustments

- **Rook pawns**: Less valuable in endgame (can't promote to escape corner)
- **Opposite-colored bishops**: Apply draw scaling factor (0.5-0.7 × score)
- **No pawns advantage**: Scale down winning chances when the stronger side has no pawns

### Passed Pawn Evaluation (Endgame Critical)

In the endgame, passed pawn evaluation dominates:

```c
int passed_pawn_bonus_eg[] = {0, 10, 20, 40, 70, 120, 200, 0};  // by rank
```

Additional endgame factors:
- **Rule of the square**: Can the king catch the pawn?
- **Unstoppable passed pawn**: Massive bonus if the pawn can't be stopped
- **King proximity to passed pawns**: Bonus for own king near own passed pawn; bonus for own king blocking opponent's passed pawn

### King Centralization

In endgame, king should be centralized. PST handles this naturally with separate endgame king PST.

### Mop-Up Evaluation

When one side has a winning material advantage and the opponent has a lone king:
- Bonus for driving opponent's king to edge/corner
- Bonus for own king being close to opponent's king

```c
int mop_up_score(int winner_king_sq, int loser_king_sq) {
    int cmd = center_manhattan_distance[loser_king_sq];  // 0-6
    int md  = manhattan_distance(winner_king_sq, loser_king_sq);
    return 4 * cmd + 14 * (14 - md);
}
```

### Syzygy Endgame Tablebases

For ultimate endgame strength, probe Syzygy tablebases (available for up to 7 pieces):
- **WDL tables**: Win/Draw/Loss information
- **DTZ tables**: Distance to zeroing move (50-move rule aware)
- For WebAssembly: probably too large (6-piece tables are ~150 GB)
- **Alternative**: Implement common endgame knowledge heuristically

---

## 9. Perft Testing

### Standard Test Positions and Expected Results

These are the canonical perft positions from the Chessprogramming wiki, used universally for move generation verification:

#### Position 1: Initial Position
**FEN:** `rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1`

| Depth | Nodes          |
|-------|----------------|
| 1     | 20             |
| 2     | 400            |
| 3     | 8,902          |
| 4     | 197,281        |
| 5     | 4,865,609      |
| 6     | 119,060,324    |
| 7     | 3,195,901,860  |

#### Position 2: Kiwipete (Complex Tactics)
**FEN:** `r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -`

| Depth | Nodes         |
|-------|---------------|
| 1     | 48            |
| 2     | 2,039         |
| 3     | 97,862        |
| 4     | 4,085,603     |
| 5     | 193,690,690   |

#### Position 3: Endgame/En Passant Stress Test
**FEN:** `8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -`

| Depth | Nodes         |
|-------|---------------|
| 1     | 14            |
| 2     | 191           |
| 3     | 2,812         |
| 4     | 43,238        |
| 5     | 674,624       |
| 6     | 11,030,083    |
| 7     | 178,633,661   |

#### Position 4: Promotion Stress Test
**FEN:** `r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1`

| Depth | Nodes         |
|-------|---------------|
| 1     | 6             |
| 2     | 264           |
| 3     | 9,467         |
| 4     | 422,333       |
| 5     | 15,833,292    |
| 6     | 706,045,033   |

#### Position 5: Promotion + Castling Edge Cases
**FEN:** `rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8`

| Depth | Nodes         |
|-------|---------------|
| 1     | 44            |
| 2     | 1,486         |
| 3     | 62,379        |
| 4     | 2,103,487     |
| 5     | 89,941,194    |

#### Position 6: Alternative Start
**FEN:** `r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10`

| Depth | Nodes          |
|-------|----------------|
| 1     | 46             |
| 2     | 2,079          |
| 3     | 89,890         |
| 4     | 3,894,594      |
| 5     | 164,075,551    |

### Perft Testing Strategy

1. Implement a `perft(depth)` function that recursively generates and counts all legal moves
2. Compare results against the tables above at each depth
3. **Divide perft**: When a mismatch is found, print per-move counts at depth 1 and compare against a known-correct engine (like Stockfish). This isolates the buggy move.
4. Test incrementally: start with Position 1 at shallow depths, then add each special case position

### Common Bugs Caught by Perft

- En passant pin: Pawn captures en passant while pinned to the king
- Castling through check: King passes through an attacked square
- Castling rights update on rook capture
- Double-push pawn setting wrong en passant square
- Promotion to all 4 piece types
- Check evasion: Missing legal moves when in check

---

## 10. Strength Targets & Key Techniques

### Realistic ELO Targets

| Implementation Level | Approximate ELO | Key Features |
|---------------------|-----------------|--------------|
| Basic (material only) | 1200-1400 | Minimax, basic eval |
| Intermediate | 1800-2200 | Alpha-beta, PSTs, quiescence |
| Strong Amateur | 2200-2600 | TT, null move, LMR, king safety |
| Expert | 2600-2900 | Full HCE, all pruning techniques |
| Master | 2900-3200 | Tuned eval, singular extensions, excellent search |

**Target for Claudefish: 2800-3200 ELO** — achievable with a well-implemented classical engine.

### Technique Priority (Implementation Order)

Listed by impact-to-effort ratio:

1. **Bitboard representation + magic bitboards** — Foundation
2. **Alpha-beta with iterative deepening** — Core search
3. **Quiescence search** — Eliminates horizon effect (~300 ELO)
4. **Piece-square tables** — Simple, huge eval improvement (~100-150 ELO)
5. **Transposition table + Zobrist hashing** — Major search speedup (~100-150 ELO)
6. **Move ordering: TT move → MVV-LVA captures → killer moves → history heuristic** (~200 ELO)
7. **Null move pruning** (~100-150 ELO)
8. **Late move reductions (LMR)** (~150-200 ELO)
9. **PVS (Principal Variation Search)** (~30-50 ELO)
10. **Aspiration windows** (~30-50 ELO)
11. **King safety evaluation** (~80-120 ELO)
12. **Pawn structure evaluation + pawn hash table** (~50-80 ELO)
13. **Piece mobility** (~40-60 ELO)
14. **Futility pruning + razoring** (~30-50 ELO)
15. **Check extensions** (~20-30 ELO)
16. **Late move pruning** (~20-30 ELO)
17. **SEE (Static Exchange Evaluation)** — Better capture ordering (~20-30 ELO)
18. **Singular extensions** (~10-20 ELO)
19. **Opening book integration** (variable, saves time in known positions)
20. **Endgame-specific evaluation** (~30-50 ELO)

### Common Pitfalls That Weaken Engines

1. **Poor move ordering**: Alpha-beta is only as good as its move ordering. Bad ordering = searching the whole tree.
2. **No quiescence search**: The engine will make tactically horrible moves.
3. **Evaluation too complex**: If evaluation is slow, you search fewer nodes. Keep it simple and fast.
4. **Hash table bugs**: Incorrect TT usage (wrong bounds, bad replacement) can cause massive strength loss and even crashes.
5. **Insufficient testing**: Use perft to verify move generation, then test against other engines.
6. **Null move in zugzwang**: Not disabling null move in K+P endgames leads to blunders.
7. **Ignoring draws**: Must detect repetition (3-fold), 50-move rule, and insufficient material.
8. **Time management**: Spending too long on easy moves or too little on critical positions.

### Move Ordering Priority

The order in which moves are tried is critical:

1. **TT/Hash move** (from transposition table)
2. **Good captures** ordered by MVV-LVA (Most Valuable Victim, Least Valuable Attacker)
3. **Killer moves** (quiet moves that caused beta cutoffs at the same ply in sibling nodes)
4. **Counter moves** (quiet move that refuted the previous move)
5. **History heuristic** (quiet moves ordered by how often they caused cutoffs)
6. **Remaining quiet moves** (ordered by history score or PST value)
7. **Bad captures** (losing captures as determined by SEE)

---

## 11. Implementation Roadmap

### Phase 1: Foundation
- [ ] Board representation (bitboards)
- [ ] FEN parsing
- [ ] Move generation (non-sliding pieces)
- [ ] Magic bitboard initialization
- [ ] Sliding piece move generation
- [ ] Make/unmake move
- [ ] Perft testing (verify against all 6 positions)

### Phase 2: Basic Search
- [ ] Negamax with alpha-beta
- [ ] Iterative deepening
- [ ] Simple evaluation (material + PST)
- [ ] Time management
- [ ] UCI protocol (basic)

### Phase 3: Core Improvements
- [ ] Transposition table + Zobrist hashing
- [ ] Quiescence search
- [ ] Move ordering (MVV-LVA, killer moves, history heuristic)
- [ ] PVS
- [ ] Null move pruning
- [ ] Check extensions

### Phase 4: Advanced Search
- [ ] Late move reductions
- [ ] Aspiration windows
- [ ] Futility pruning
- [ ] Razoring
- [ ] Late move pruning
- [ ] SEE (Static Exchange Evaluation)

### Phase 5: Evaluation
- [ ] Tapered evaluation (mg/eg)
- [ ] Pawn structure + pawn hash table
- [ ] King safety
- [ ] Piece mobility
- [ ] Bishop pair
- [ ] Rook on open/semi-open file
- [ ] Passed pawns (comprehensive)
- [ ] Knight outposts
- [ ] Endgame-specific adjustments

### Phase 6: Polish
- [ ] Opening book (Polyglot)
- [ ] Draw detection (repetition, 50-move, insufficient material)
- [ ] Pondering (thinking on opponent's time)
- [ ] Full UCI protocol compliance
- [ ] WebAssembly compilation and testing

---

## References & Sources

- **Chessprogramming Wiki**: https://www.chessprogramming.org/ — The primary reference for all chess programming topics
- **Bitboards**: https://www.chessprogramming.org/Bitboards
- **Magic Bitboards**: https://www.chessprogramming.org/Magic_Bitboards
  - Analog Hors: "Magical Bitboards and How to Find Them" — https://analog-hors.github.io/site/magic-bitboards/
  - Rhys Rustad-Elliott: "Fast Chess Move Generation With Magic Bitboards" — https://rhysre.net/fast-chess-move-generation-with-magic-bitboards.html
- **PVS**: https://www.chessprogramming.org/Principal_Variation_Search
- **Aspiration Windows**: https://www.chessprogramming.org/Aspiration_Windows
- **Null Move Pruning**: https://www.chessprogramming.org/Null_Move_Pruning
- **LMR**: https://www.chessprogramming.org/Late_Move_Reductions
- **Futility Pruning**: https://www.chessprogramming.org/Futility_Pruning
- **Evaluation**: https://www.chessprogramming.org/Evaluation
- **King Safety**: https://www.chessprogramming.org/King_Safety
- **Transposition Table**: https://www.chessprogramming.org/Transposition_Table
- **Zobrist Hashing**: https://www.chessprogramming.org/Zobrist_Hashing
- **Perft Results**: https://www.chessprogramming.org/Perft_Results
- **Move Ordering**: https://www.chessprogramming.org/Move_Ordering
- **MVV-LVA**: https://www.chessprogramming.org/MVV-LVA
- **History Heuristic**: https://www.chessprogramming.org/History_Heuristic
- **Polyglot Opening Book**: https://www.chessprogramming.org/PolyGlot
- **Opening Book**: https://www.chessprogramming.org/Opening_Book
- **Quiescence Search**: https://www.chessprogramming.org/Quiescence_Search
- **Endgame Tablebases**: https://www.chessprogramming.org/Endgame_Tablebases
- **Syzygy Bases**: https://www.chessprogramming.org/Syzygy_Bases
- **Rustic Chess Engine Book** (bitboards tutorial): https://rustic-chess.org/board_representation/bitboards.html
- **Mediocre Chess Guides**: https://mediocrechess.sourceforge.net/guides/
- **Strongest non-NN engine discussion**: https://chess.stackexchange.com/questions/39098/
- **ELO comparison of techniques**: https://chess.stackexchange.com/questions/40917/
- **billchow98/chess**: 2500+ ELO C++20 engine — https://github.com/billchow98/chess
