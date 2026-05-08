#!/usr/bin/env python3
"""
Convert a Stockfish HalfKP .nnue file to Claudefish format.

Stockfish HalfKP format (SF12-SF16, halfkp_256x2-32-32):
  Header:
    uint32: version
    uint32: global_hash
    uint32: description_length
    char[description_length]: description string (not null-terminated)
  Feature Transformer Section:
    uint32: ft_section_hash
    int16[ACCUMULATOR_DIM]: ft_biases (256 values)
    int16[INPUT_DIM * ACCUMULATOR_DIM]: ft_weights (40960 * 256 values, row-major)
  Network Section:
    uint32: net_section_hash
    int32[H1]: h1_biases (32)
    int8[2*ACCUMULATOR_DIM * H1]: h1_weights (512 * 32)
    int32[H2]: h2_biases (32)
    int8[H1 * H2]: h2_weights (32 * 32)
    int32: out_bias (1)
    int8[H2]: out_weights (32)

Claudefish binary format:
  Header:
    uint32: magic (0x4E4E5545 = "NNUE")
    uint32: version (7)
    char[256]: description (null-padded, fixed 256 bytes)
  Data (biases BEFORE weights for each layer):
    int16[256]: ft_biases
    int16[40960 * 256]: ft_weights
    int32[32]: h1_biases
    int8[512 * 32]: h1_weights
    int32[32]: h2_biases
    int8[32 * 32 + 32]: h2_weights + out_weights (contiguous in one buffer)
    int32: out_bias

Usage:
  python3 scripts/convert_nnue.py <stockfish_net.nnue> engine/net.nnue
"""

import struct
import sys
import os

CLAUDEFISH_MAGIC = 0x4E4E5545  # "NNUE"
CLAUDEFISH_VERSION = 7

INPUT_DIM = 41024         # 64 king squares * 641 features/king (Stockfish HalfKP "Friend" layout)
ACCUMULATOR_DIM = 256
HIDDEN1_DIM = 32
HIDDEN2_DIM = 32

FT_BIAS_BYTES   = ACCUMULATOR_DIM * 2              # int16
FT_WEIGHT_BYTES = INPUT_DIM * ACCUMULATOR_DIM * 2  # int16
H1_BIAS_BYTES   = HIDDEN1_DIM * 4                  # int32
H1_WEIGHT_BYTES = ACCUMULATOR_DIM * 2 * HIDDEN1_DIM # int8: 512 * 32
H2_BIAS_BYTES   = HIDDEN2_DIM * 4                  # int32
H2_WEIGHT_BYTES = HIDDEN1_DIM * HIDDEN2_DIM        # int8: 32 * 32
OUT_WEIGHT_BYTES = HIDDEN2_DIM                     # int8: 32
OUT_BIAS_BYTES  = 4                                # int32


def read_sf_nnue(path):
    with open(path, 'rb') as f:
        data = f.read()

    total = len(data)
    offset = 0

    def read_u32():
        nonlocal offset
        v = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        return v

    def read_raw(n):
        nonlocal offset
        if offset + n > len(data):
            raise ValueError(f"Tried to read {n} bytes at offset {offset} but only {len(data)-offset} remain")
        v = data[offset:offset + n]
        offset += n
        return v

    # Global header
    version   = read_u32()
    glob_hash = read_u32()
    desc_size = read_u32()
    desc_bytes = read_raw(desc_size)
    desc = desc_bytes.decode('utf-8', errors='replace')

    print(f"SF version      : 0x{version:08X}")
    print(f"Global hash     : 0x{glob_hash:08X}")
    print(f"Description     : {desc[:120]}")
    print(f"Header ends at  : {offset} / {total}")

    # Feature transformer section
    ft_hash    = read_u32()
    print(f"FT section hash : 0x{ft_hash:08X}")
    ft_biases  = read_raw(FT_BIAS_BYTES)
    ft_weights = read_raw(FT_WEIGHT_BYTES)
    print(f"FT data ends at : {offset} / {total}")

    # Network section
    net_hash    = read_u32()
    print(f"Net section hash: 0x{net_hash:08X}")
    h1_biases   = read_raw(H1_BIAS_BYTES)
    h1_weights  = read_raw(H1_WEIGHT_BYTES)
    h2_biases   = read_raw(H2_BIAS_BYTES)
    h2_weights  = read_raw(H2_WEIGHT_BYTES)
    out_bias    = read_raw(OUT_BIAS_BYTES)
    out_weights = read_raw(OUT_WEIGHT_BYTES)

    print(f"All data ends at: {offset} / {total}")
    if offset != total:
        leftover = total - offset
        print(f"WARNING: {leftover} bytes unread — possible format variant or trailing padding")

    return {
        'description': desc,
        'ft_biases':   ft_biases,
        'ft_weights':  ft_weights,
        'h1_biases':   h1_biases,
        'h1_weights':  h1_weights,
        'h2_biases':   h2_biases,
        'h2_weights':  h2_weights,
        'out_bias':    out_bias,
        'out_weights': out_weights,
    }


def write_claudefish_nnue(path, w):
    orig = w['description']
    desc_str = f"HalfKP[41024]->256x2->32->32->1 from SF: {orig}"
    desc_enc = desc_str[:255].encode('utf-8')
    desc_padded = desc_enc + b'\x00' * (256 - len(desc_enc))

    expected = (8 + 256 +
                FT_BIAS_BYTES + FT_WEIGHT_BYTES +
                H1_BIAS_BYTES + H1_WEIGHT_BYTES +
                H2_BIAS_BYTES +
                H2_WEIGHT_BYTES + OUT_WEIGHT_BYTES +  # contiguous block
                OUT_BIAS_BYTES)

    with open(path, 'wb') as f:
        f.write(struct.pack('<II', CLAUDEFISH_MAGIC, CLAUDEFISH_VERSION))
        f.write(desc_padded)           # 256 bytes fixed description
        f.write(w['ft_biases'])        # int16[256]
        f.write(w['ft_weights'])       # int16[40960*256]
        f.write(w['h1_biases'])        # int32[32]
        f.write(w['h1_weights'])       # int8[512*32]
        f.write(w['h2_biases'])        # int32[32]
        f.write(w['h2_weights'])       # int8[32*32]  \  contiguous buffer in Claudefish format
        f.write(w['out_weights'])      # int8[32]     /
        f.write(w['out_bias'])         # int32

    actual = os.path.getsize(path)
    print(f"Wrote {path}: {actual} bytes (expected {expected})")
    if actual != expected:
        print(f"ERROR: size mismatch ({actual} vs {expected}) — check network architecture")
        sys.exit(1)
    print("OK: size matches Claudefish HalfKP[40960]->256x2->32->32->1 format")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        print(f"Usage: {sys.argv[0]} <stockfish_net.nnue> <output_net.nnue>")
        sys.exit(1)

    inp  = sys.argv[1]
    outp = sys.argv[2]

    if not os.path.exists(inp):
        print(f"ERROR: input file not found: {inp}")
        sys.exit(1)

    print(f"\nReading  : {inp} ({os.path.getsize(inp):,} bytes)")
    weights = read_sf_nnue(inp)

    print(f"\nWriting  : {outp}")
    write_claudefish_nnue(outp, weights)

    print("\nConversion complete. Embed in WASM build via:")
    print(f"  --embed-file {outp}@/net.nnue")


if __name__ == '__main__':
    main()
