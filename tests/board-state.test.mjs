import assert from "node:assert/strict";
import test from "node:test";

import { Chess } from "chess.js";

const {
  applyMoveToPieces,
  createPiecesFromFen,
  findPieceAtSquare,
  getKingSquare,
} = await import("../src/components/Board/boardState.ts");

test("createPiecesFromFen builds all opening pieces and kings on their starting squares", () => {
  const pieces = createPiecesFromFen(new Chess().fen());

  assert.equal(pieces.length, 32);
  assert.equal(getKingSquare(pieces, "w"), "e1");
  assert.equal(getKingSquare(pieces, "b"), "e8");
  assert.equal(findPieceAtSquare(pieces, "d1")?.type, "q");
  assert.equal(findPieceAtSquare(pieces, "a7")?.color, "b");
});

test("applyMoveToPieces animates castling by moving both king and rook", () => {
  const chess = new Chess("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  const pieces = createPiecesFromFen(chess.fen());
  const move = chess.move({ from: "e1", to: "g1" });

  assert.ok(move, "castling move should be legal");

  const nextPieces = applyMoveToPieces(pieces, move);

  assert.equal(findPieceAtSquare(nextPieces, "g1")?.type, "k");
  assert.equal(findPieceAtSquare(nextPieces, "f1")?.type, "r");
  assert.equal(findPieceAtSquare(nextPieces, "e1"), null);
  assert.equal(findPieceAtSquare(nextPieces, "h1"), null);
});

test("applyMoveToPieces removes the adjacent pawn during en passant", () => {
  const chess = new Chess();
  chess.move({ from: "e2", to: "e4" });
  chess.move({ from: "a7", to: "a6" });
  chess.move({ from: "e4", to: "e5" });
  chess.move({ from: "d7", to: "d5" });

  const pieces = createPiecesFromFen(chess.fen());
  const move = chess.move({ from: "e5", to: "d6" });

  assert.ok(move, "en passant move should be legal");

  const nextPieces = applyMoveToPieces(pieces, move);

  assert.equal(findPieceAtSquare(nextPieces, "d6")?.type, "p");
  assert.equal(findPieceAtSquare(nextPieces, "d5"), null);
  assert.equal(findPieceAtSquare(nextPieces, "e5"), null);
});

test("applyMoveToPieces changes the moving pawn into the selected promotion piece", () => {
  const chess = new Chess("8/4P3/8/8/8/8/8/k6K w - - 0 1");
  const pieces = createPiecesFromFen(chess.fen());
  const move = chess.move({ from: "e7", to: "e8", promotion: "n" });

  assert.ok(move, "promotion move should be legal");

  const nextPieces = applyMoveToPieces(pieces, move);

  assert.equal(findPieceAtSquare(nextPieces, "e8")?.type, "n");
  assert.equal(findPieceAtSquare(nextPieces, "e7"), null);
});
