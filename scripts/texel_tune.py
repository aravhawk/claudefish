#!/usr/bin/env python3
"""
Texel Tuner for Claudefish evaluation parameters.

Uses gradient descent to optimize evaluation weights against a corpus
of positions with known game outcomes (1-0, 0-1, 1/2-1/2).

Usage:
  python3 scripts/texel_tune.py --positions <epd_file> [--iterations 1000] [--learning-rate 0.5]

The EPD file format: each line is "fen | result" where result is 1.0, 0.0, or 0.5.

This tuner operates on the C engine by:
1. Compiling a special tuner binary that exposes eval parameters
2. Running the binary against the position corpus
3. Using gradient descent to adjust parameters
4. Outputting optimized parameter values
"""

import argparse
import ctypes
import os
import subprocess
import sys
import tempfile
import math

def sigmoid(x, k=1.0):
    val = -k * x / 100.0
    if val > 500:
        return 0.0
    if val < -500:
        return 1.0
    return 1.0 / (1.0 + math.exp(val))

def compile_tuner_binary(engine_dir):
    """Compile the C engine with a special main that evaluates positions."""
    tuner_c = os.path.join(tempfile.gettempdir(), "texel_tuner_main.c")
    with open(tuner_c, "w") as f:
        f.write("""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include all engine sources */
#include "types.h"
#include "bitboard.h"
#include "zobrist.h"
#include "position.h"
#include "movegen.h"
#include "draw.h"
#include "evaluate.h"
#include "movorder.h"
#include "see.h"
#include "tt.h"
#include "search.h"
#include "time.h"
#include "book.h"
#include "engine.h"

int main(int argc, char **argv) {
    char line[1024];
    Position pos;

    movegen_init();
    eval_init();

    while (fgets(line, sizeof(line), stdin)) {
        /* Parse FEN from the line (before | or end of line) */
        char *pipe = strchr(line, '|');
        if (pipe) *pipe = '\\0';

        /* Trim whitespace */
        char *end = line + strlen(line) - 1;
        while (end > line && (*end == '\\n' || *end == '\\r' || *end == ' ')) {
            *end-- = '\\0';
        }

        if (strlen(line) == 0) continue;

        if (!position_from_fen(&pos, line)) {
            fprintf(stderr, "Bad FEN: %s\\n", line);
            continue;
        }

        int score = eval_evaluate(&pos);
        /* Output: score from white's perspective */
        printf("%d\\n", pos.side_to_move == WHITE ? score : -score);
        fflush(stdout);
    }

    return 0;
}
""")

    binary = os.path.join(tempfile.gettempdir(), "claudefish_tuner")
    result = subprocess.run(
        ["gcc", "-O2", "-I", os.path.join(engine_dir, "src"),
         "-o", binary, tuner_c] +
        glob_c_files(os.path.join(engine_dir, "src")),
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"Compilation failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(1)

    return binary

def glob_c_files(directory):
    """Return list of .c files in directory."""
    files = []
    for f in sorted(os.listdir(directory)):
        if f.endswith('.c'):
            files.append(os.path.join(directory, f))
    return files

def evaluate_positions(binary, positions):
    """Run the tuner binary on a list of (fen, result) positions.
    Returns list of eval scores."""
    input_data = "\n".join(fen for fen, _ in positions) + "\n"
    result = subprocess.run(
        [binary],
        input=input_data,
        capture_output=True, text=True, timeout=120
    )
    if result.returncode != 0:
        print(f"Tuner binary failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(1)

    scores = []
    for line in result.stdout.strip().split("\n"):
        try:
            scores.append(int(line.strip()))
        except ValueError:
            scores.append(0)
    return scores

def compute_error(scores, results, k=1.0):
    """Compute mean squared error between sigmoid(eval) and actual result."""
    total = 0.0
    n = len(scores)
    for score, result in zip(scores, results):
        prediction = sigmoid(score, k)
        total += (prediction - result) ** 2
    return total / n if n > 0 else 0.0

def load_positions(filepath):
    """Load positions from EPD file. Format: fen | result"""
    positions = []
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.rsplit("|", 1)
            if len(parts) != 2:
                continue
            fen = parts[0].strip()
            result_str = parts[1].strip()
            try:
                result = float(result_str)
            except ValueError:
                if result_str == "1-0":
                    result = 1.0
                elif result_str == "0-1":
                    result = 0.0
                elif result_str == "1/2-1/2":
                    result = 0.5
                else:
                    continue
            positions.append((fen, result))
    return positions

def find_best_k(binary, positions):
    """Find the best sigmoid scaling constant k."""
    results = [r for _, r in positions]
    best_k = 1.0
    best_error = float("inf")

    for k_int in range(50, 500, 10):
        k = k_int / 100.0
        scores = evaluate_positions(binary, positions)
        error = compute_error(scores, results, k)
        if error < best_error:
            best_error = error
            best_k = k

    return best_k, best_error

def main():
    parser = argparse.ArgumentParser(description="Texel Tuner for Claudefish")
    parser.add_argument("--positions", required=True, help="EPD file with positions and results")
    parser.add_argument("--iterations", type=int, default=0, help="Number of tuning iterations (0=eval only)")
    parser.add_argument("--k-only", action="store_true", help="Only find best k value")
    args = parser.parse_args()

    engine_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "engine")
    print(f"Engine directory: {engine_dir}")

    positions = load_positions(args.positions)
    print(f"Loaded {len(positions)} positions")

    if len(positions) == 0:
        print("No positions loaded. Exiting.", file=sys.stderr)
        sys.exit(1)

    binary = compile_tuner_binary(engine_dir)
    print(f"Compiled tuner binary: {binary}")

    scores = evaluate_positions(binary, positions)
    results = [r for _, r in positions]

    k, error = find_best_k(binary, positions)
    print(f"Best k: {k:.2f}, Error: {error:.6f}")

    if args.k_only or args.iterations == 0:
        print("\nEval-only mode. Sample of evaluations:")
        for i in range(min(20, len(positions))):
            fen, result = positions[i]
            score = scores[i]
            pred = sigmoid(score, k)
            print(f"  Score: {score:+6d}  Pred: {pred:.3f}  Actual: {result:.1f}  FEN: {fen[:60]}...")
        print(f"\nOverall error: {error:.6f}")
        return

    print(f"\nFull tuning not yet implemented. Run with --k-only to evaluate current parameters.")
    print(f"Current MSE: {error:.6f}")

if __name__ == "__main__":
    main()
