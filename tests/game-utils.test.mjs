import test from "node:test";
import assert from "node:assert/strict";

import { Chess } from "chess.js";

import {
  buildMoveHistoryRows,
  collectCapturedPieces,
  DIFFICULTY_LEVELS,
  formatEvaluationLabel,
  getDifficultyConfig,
  getEvaluationFill,
  getGameResult,
  getUndoPlyCount,
  parseUciMove,
  replayGame,
  shouldShowLoadingOverlay,
  shouldEngineMove,
  toWhiteCentipawns,
} from "../src/app/gameUtils.ts";

test("parseUciMove handles standard moves and promotions", () => {
  assert.deepEqual(parseUciMove("e2e4"), {
    from: "e2",
    to: "e4",
  });
  assert.deepEqual(parseUciMove("a7a8q"), {
    from: "a7",
    to: "a8",
    promotion: "q",
  });
  assert.equal(parseUciMove("bad"), null);
});

test("difficulty helpers expose the exact configured engine settings", () => {
  assert.deepEqual(
    DIFFICULTY_LEVELS.map(({ key, depth, timeMs }) => ({ key, depth, timeMs })),
    [
      { key: "easy", depth: 2, timeMs: 200 },
      { key: "medium", depth: 4, timeMs: 1000 },
      { key: "hard", depth: 6, timeMs: 3000 },
      { key: "maximum", depth: 12, timeMs: 5000 },
    ],
  );
  assert.equal(getDifficultyConfig("maximum").label, "Maximum");
});

test("formatEvaluationLabel formats centipawns and mate scores", () => {
  assert.equal(formatEvaluationLabel(34), "+0.3");
  assert.equal(formatEvaluationLabel(-56), "-0.6");
  assert.equal(formatEvaluationLabel(30000), "M1");
  assert.equal(formatEvaluationLabel(-29998), "-M1");
});

test("getEvaluationFill centers neutral positions and saturates mate scores", () => {
  assert.equal(getEvaluationFill(null), 50);
  assert.ok(getEvaluationFill(120) > 50);
  assert.ok(getEvaluationFill(-120) < 50);
  assert.equal(getEvaluationFill(30000), 100);
  assert.equal(getEvaluationFill(-30000), 0);
});

test("toWhiteCentipawns converts side-to-move-relative engine scores", () => {
  assert.equal(
    toWhiteCentipawns(-80, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"),
    80,
  );
  assert.equal(
    toWhiteCentipawns(55, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
    55,
  );
});

test("shouldEngineMove only returns true when it is black to move", () => {
  assert.equal(
    shouldEngineMove("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
    false,
  );
  assert.equal(
    shouldEngineMove("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"),
    true,
  );
});

test("shouldShowLoadingOverlay hides the loader when engine initialization fails", () => {
  assert.equal(
    shouldShowLoadingOverlay({
      engineError: null,
      isReady: false,
      loadingDismissed: false,
    }),
    true,
  );
  assert.equal(
    shouldShowLoadingOverlay({
      engineError: "worker init failed",
      isReady: false,
      loadingDismissed: false,
    }),
    false,
  );
});

test("replayGame rebuilds the current position from stored moves", () => {
  const rebuilt = replayGame([
    { from: "e2", to: "e4" },
    { from: "e7", to: "e5" },
    { from: "g1", to: "f3" },
  ]);
  const direct = new Chess();
  direct.move("e4");
  direct.move("e5");
  direct.move("Nf3");

  assert.equal(rebuilt.fen(), direct.fen());
  assert.deepEqual(
    rebuilt.history({ verbose: true }).map((move) => move.san),
    ["e4", "e5", "Nf3"],
  );
});

test("replayGame preserves repetition history from the stored move list", () => {
  const repeatedGame = replayGame([
    { from: "g1", to: "f3" },
    { from: "g8", to: "f6" },
    { from: "f3", to: "g1" },
    { from: "f6", to: "g8" },
    { from: "g1", to: "f3" },
    { from: "g8", to: "f6" },
    { from: "f3", to: "g1" },
    { from: "f6", to: "g8" },
  ]);

  assert.equal(repeatedGame.isThreefoldRepetition(), true);
  assert.equal(getGameResult(repeatedGame)?.title, "Threefold repetition — Draw");
});

test("buildMoveHistoryRows formats SAN moves into numbered pairs", () => {
  const game = new Chess();
  game.move("e4");
  game.move("e5");
  game.move("Nf3");

  assert.deepEqual(buildMoveHistoryRows(game.history({ verbose: true })), [
    { moveNumber: 1, white: "e4", black: "e5" },
    { moveNumber: 2, white: "Nf3", black: null },
  ]);
});

test("collectCapturedPieces groups captured material by side", () => {
  const game = new Chess();

  game.move("e4");
  game.move("d5");
  game.move("exd5");
  game.move("Qxd5");

  assert.deepEqual(collectCapturedPieces(game.history({ verbose: true })), {
    white: ["p"],
    black: ["P"],
  });
});

test("getUndoPlyCount removes a pending player move or a full move pair", () => {
  assert.equal(
    getUndoPlyCount("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", 1),
    1,
  );
  assert.equal(
    getUndoPlyCount("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", 2),
    2,
  );
  assert.equal(
    getUndoPlyCount("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0),
    0,
  );
});

test("getGameResult reports checkmate and stalemate states", () => {
  const checkmateGame = new Chess(
    "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
  );
  const stalemateGame = new Chess("7k/5Q2/7K/8/8/8/8/8 b - - 0 1");

  assert.equal(getGameResult(checkmateGame)?.title, "Checkmate — Claudefish wins.");
  assert.equal(getGameResult(stalemateGame)?.title, "Stalemate — Draw");
});

test("getGameResult reports the remaining frontend draw conditions", () => {
  const repetitionGame = new Chess();
  const repetitionCycle = ["Nf3", "Nf6", "Ng1", "Ng8"];

  for (let count = 0; count < 3; count += 1) {
    for (const move of repetitionCycle) {
      repetitionGame.move(move);
    }
  }

  const insufficientMaterialGame = new Chess("8/8/8/8/8/8/7k/6BK w - - 0 1");
  const fiftyMoveGame = new Chess("7k/8/8/8/8/8/R7/K7 w - - 100 1");

  assert.equal(getGameResult(repetitionGame)?.title, "Threefold repetition — Draw");
  assert.equal(getGameResult(insufficientMaterialGame)?.title, "Insufficient material — Draw");
  assert.equal(getGameResult(fiftyMoveGame)?.title, "50-move rule — Draw");
});
