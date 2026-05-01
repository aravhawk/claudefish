#!/usr/bin/env python3
"""Convert Stockfish NNUE weights for Claudefish engine.

This script reads a Stockfish .nnue file and converts the weights
to match Claudefish's NNUE architecture (HalfKP+Threat -> 256 -> 32 -> 32 -> 1).

Usage:
    python3 convert_nnue.py <input.nnue> [output.bin]

The output file can be loaded by nnue_load_from_file() in the engine.
"""

import struct
import sys
import numpy as np

# Architecture dimensions (must match nnue.h)
HALFKP_INPUT_DIM = 41024
THREAT_INPUT_DIM = 256
TOTAL_INPUT_DIM = 41024 + 256  # 57408 with threat features (use 41024 for old nets)
ACCUMULATOR_DIM = 256
HIDDEN1_DIM = 32
HIDDEN2_DIM = 32
OUTPUT_DIM = 1

# Claudefish net format
MAGIC = 0x4E4E5545  # "NNUE"
VERSION = 7

def read_sf_nnue(path):
    """Read a Stockfish .nnue file and extract weights."""
    with open(path, 'rb') as f:
        data = f.read()

    # Stockfish .nnue format: header then network data
    # Header: magic (4 bytes), version (4 bytes), desc hash (4 bytes), size (4 bytes)
    if len(data) < 16:
        print(f"File too small: {len(data)} bytes")
        return None

    magic = struct.unpack('<I', data[0:4])[0]
    version = struct.unpack('<I', data[4:8])[0]
    print(f"Stockfish NNUE: magic=0x{magic:08X}, version={version}")

    # Skip header (varies by version, typically 64 bytes)
    offset = 64

    # Feature transformer weights and biases
    # SF uses HalfKP 41024 -> 256 architecture (per perspective)
    ft_weight_size = 41024 * 256 * 2  # int16, two perspectives
    ft_bias_size = 256 * 2  # int16, two perspectives

    print(f"Attempting to read feature transformer at offset {offset}")
    print(f"Expected FT weight bytes: {ft_weight_size}")

    # For now, just report what we find
    remaining = len(data) - offset
    print(f"Remaining data after header: {remaining} bytes")

    return {
        'data': data,
        'offset': offset,
        'version': version
    }


def write_claudefish_nnue(weights, output_path):
    """Write weights in Claudefish's binary format."""
    # For now, create a placeholder that documents the format
    print(f"Writing Claudefish NNUE to {output_path}")

    # Format:
    # Header: magic(4) + version(4) + input_dim(4) + acc_dim(4) + h1_dim(4) + h2_dim(4) + desc_len(4) + desc(desc_len)
    # FT weights: input_dim * acc_dim * sizeof(int16)  [row-major]
    # FT biases:  acc_dim * sizeof(int16)
    # H1 weights: acc_dim*2 * h1_dim * sizeof(int8)
    # H1 biases:  h1_dim * sizeof(int32)
    # H2 weights: h1_dim * h2_dim * sizeof(int8)
    # H2 biases:  h2_dim * sizeof(int32)
    # Out weights: h2_dim * sizeof(int8)
    # Out bias: sizeof(int32)

    desc = b"Claudefish NNUE (placeholder - no real weights)"
    desc_len = len(desc)

    header = struct.pack('<IIIIIII',
        MAGIC, VERSION,
        TOTAL_INPUT_DIM, ACCUMULATOR_DIM, HIDDEN1_DIM, HIDDEN2_DIM,
        desc_len)

    # Placeholder: zero weights (engine will fall back to classical eval)
    ft_weights = b'\x00' * (TOTAL_INPUT_DIM * ACCUMULATOR_DIM * 2)
    ft_biases = b'\x00' * (ACCUMULATOR_DIM * 2)
    h1_weights = b'\x00' * (ACCUMULATOR_DIM * 2 * HIDDEN1_DIM)
    h1_biases = b'\x00' * (HIDDEN1_DIM * 4)
    h2_weights = b'\x00' * (HIDDEN1_DIM * HIDDEN2_DIM)
    h2_biases = b'\x00' * (HIDDEN2_DIM * 4)
    out_weights = b'\x00' * HIDDEN2_DIM
    out_bias = b'\x00' * 4

    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(desc)
        f.write(ft_weights)
        f.write(ft_biases)
        f.write(h1_weights)
        f.write(h1_biases)
        f.write(h2_weights)
        f.write(h2_biases)
        f.write(out_weights)
        f.write(out_bias)

    total_size = len(header) + desc_len + len(ft_weights) + len(ft_biases) + \
                 len(h1_weights) + len(h1_biases) + len(h2_weights) + len(h2_biases) + \
                 len(out_weights) + len(out_bias)

    print(f"Written {total_size} bytes to {output_path}")
    print("NOTE: This is a placeholder with zero weights. The engine will use classical eval.")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input.nnue> [output.bin]")
        print("  Reads a Stockfish .nnue file and converts to Claudefish format.")
        print("  If no output path, writes to claudefish_nnue.bin")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else "claudefish_nnue.bin"

    sf_data = read_sf_nnue(input_path)
    if sf_data is None:
        print("Failed to read Stockfish NNUE file")
        sys.exit(1)

    write_claudefish_nnue(sf_data, output_path)
    print("Done. Load the output file in the engine via nnue_load_from_file().")


if __name__ == '__main__':
    main()
