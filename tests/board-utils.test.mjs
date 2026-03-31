import assert from "node:assert/strict";
import test from "node:test";

const { PIECE_GLYPHS, buildBoardSquares, parseFenPlacement } = await import(
  "../src/components/Board/boardUtils.ts"
);

const STARTING_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

test("buildBoardSquares maps the starting position, labels, and orientation correctly", () => {
  const squares = buildBoardSquares(STARTING_FEN);

  assert.equal(squares.length, 64);
  assert.equal(
    squares.filter((square) => square.piece !== null).length,
    32,
    "expected all 32 starting pieces to be present",
  );

  assert.deepEqual(
    squares.slice(0, 8).map((square) => square.square),
    ["a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8"],
  );

  assert.equal(squares.at(-1)?.square, "h1");
  assert.equal(squares.find((square) => square.square === "a1")?.piece, "R");
  assert.equal(squares.find((square) => square.square === "e1")?.piece, "K");
  assert.equal(squares.find((square) => square.square === "d8")?.piece, "q");
  assert.equal(squares.find((square) => square.square === "a1")?.isLight, false);
  assert.equal(squares.find((square) => square.square === "a8")?.isLight, true);
  assert.equal(squares.find((square) => square.square === "a8")?.rankLabel, "8");
  assert.equal(squares.find((square) => square.square === "a1")?.fileLabel, "a");
  assert.equal(squares.find((square) => square.square === "h1")?.fileLabel, "h");
});

test("parseFenPlacement expands each rank to eight files", () => {
  const board = parseFenPlacement("8/8/3p4/8/8/8/8/4K3 w - - 0 1");

  assert.equal(board.length, 8);
  assert.deepEqual(board[2], [null, null, null, "p", null, null, null, null]);
  assert.deepEqual(board[7], [null, null, null, null, "K", null, null, null]);
});

test("all chess piece glyphs are visually distinct", () => {
  assert.equal(Object.keys(PIECE_GLYPHS).length, 12);
  assert.equal(new Set(Object.values(PIECE_GLYPHS)).size, 12);
});
