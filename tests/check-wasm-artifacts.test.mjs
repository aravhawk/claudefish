import assert from "node:assert/strict";
import { createRequire } from "node:module";
import test from "node:test";
import { readFile, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Chess } from "chess.js";

const require = createRequire(import.meta.url);
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const engineJsPath = path.join(repoRoot, "public", "engine", "engine.js");
const engineWasmPath = path.join(repoRoot, "public", "engine", "engine.wasm");
const expectedExports = [
  "init_engine",
  "set_position",
  "search_best_move",
  "evaluate_position",
  "get_legal_moves",
];
const UCI_MOVE_REGEX = /^[a-h][1-8][a-h][1-8][qrbn]?$/;
const START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const CASTLING_FEN = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
const PROMOTION_FEN = "6k1/4P3/8/8/8/8/8/6K1 w - - 0 1";
const MEMORY_TEST_FEN = "4k3/8/3p4/3P4/3K4/8/8/8 w - - 0 1";
const REPETITION_START_FEN = "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const REPETITION_MOVES = ["g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"];
const FRONTEND_FLOW_START_FEN = "8/8/8/k7/4q3/8/K7/2Q5 w - - 0 1";
const FRONTEND_FLOW_USER_MOVES = ["c1g1", "a2a3", "a3a2", "a2a3", "a3a2"];
const FRONTEND_FLOW_ENGINE_MOVES = ["e4c2", "c2c3", "c3c2", "c2c3", "c3c2"];

function setGlobalProperty(name, value) {
  Object.defineProperty(globalThis, name, {
    configurable: true,
    enumerable: true,
    value,
    writable: true,
  });
}

function restoreGlobalProperty(name, descriptor) {
  if (descriptor) {
    Object.defineProperty(globalThis, name, descriptor);
    return;
  }

  delete globalThis[name];
}

function splitMoves(movesCsv) {
  return movesCsv.length === 0 ? [] : movesCsv.split(",").filter(Boolean);
}

function createEngineApi(engineModule) {
  return {
    initEngine: engineModule.cwrap("init_engine", "number", []),
    setPosition: engineModule.cwrap("set_position", "number", ["string"]),
    searchBestMove: engineModule.cwrap("search_best_move", "string", ["number", "number"]),
    evaluatePosition: engineModule.cwrap("evaluate_position", "number", []),
    getLegalMovesCsv: engineModule.cwrap("get_legal_moves", "string", []),
  };
}

async function instantiateEngine({ captureMemory = false } = {}) {
  const wasmBuffer = await readFile(engineWasmPath);
  const originalGlobals = new Map([
    ["process", Object.getOwnPropertyDescriptor(globalThis, "process")],
    ["window", Object.getOwnPropertyDescriptor(globalThis, "window")],
    ["document", Object.getOwnPropertyDescriptor(globalThis, "document")],
  ]);

  let memory = null;

  setGlobalProperty("process", undefined);
  setGlobalProperty("window", globalThis);
  setGlobalProperty("document", {
    currentScript: {
      src: `file://${engineJsPath}`,
    },
  });

  try {
    delete require.cache[require.resolve(engineJsPath)];
    const createChessEngine = require(engineJsPath);
    const engineModule = await createChessEngine({
      locateFile: () => engineWasmPath,
      print: () => {},
      printErr: () => {},
      wasmBinary: wasmBuffer,
      ...(captureMemory
        ? {
            instantiateWasm(imports, receiveInstance) {
              void WebAssembly.instantiate(wasmBuffer, imports).then(({ instance, module }) => {
                if (instance.exports.memory instanceof WebAssembly.Memory) {
                  memory = instance.exports.memory;
                }

                receiveInstance(instance, module);
              });

              return {};
            },
          }
        : {}),
    });

    return { engineModule, memory };
  } finally {
    for (const [name, descriptor] of originalGlobals.entries()) {
      restoreGlobalProperty(name, descriptor);
    }
  }
}

test("engine build outputs exist and are non-empty", async () => {
  const [engineJsStat, engineWasmStat] = await Promise.all([
    stat(engineJsPath),
    stat(engineWasmPath),
  ]);

  assert.ok(engineJsStat.size > 0, "engine.js should be non-empty");
  assert.ok(engineWasmStat.size > 0, "engine.wasm should be non-empty");

  const engineJs = await readFile(engineJsPath, "utf8");
  assert.match(engineJs, /createChessEngine/, "engine.js should expose createChessEngine");
});

test("engine.wasm exports the required C API", async () => {
  const wasmBuffer = await readFile(engineWasmPath);
  assert.ok(WebAssembly.validate(wasmBuffer), "engine.wasm should be a valid WebAssembly binary");

  const wasmModule = await WebAssembly.compile(wasmBuffer);
  const exports = new Set(WebAssembly.Module.exports(wasmModule).map((entry) => entry.name));

  for (const exportName of expectedExports) {
    assert.ok(exports.has(exportName), `missing export: ${exportName}`);
  }
});

test("engine module initializes successfully in a Node.js test context", async () => {
  const { engineModule } = await instantiateEngine();

  assert.equal(typeof engineModule._init_engine, "function", "engine module should expose _init_engine");
  assert.equal(engineModule._init_engine(), 0, "init_engine should return success");
});

test("embedded Polyglot book is available in the WASM runtime", async () => {
  const { engineModule } = await instantiateEngine();
  const bookBytes = engineModule.FS?.readFile?.("/book.bin");

  assert.ok(bookBytes instanceof Uint8Array, "book.bin should be readable from the embedded filesystem");
  assert.ok(bookBytes.length > 2_000_000, `expected real opening book data, got ${bookBytes.length} bytes`);
});

test("engine API returns the expected types and move count", async () => {
  const { engineModule } = await instantiateEngine();
  const api = createEngineApi(engineModule);

  const initStatus = api.initEngine();
  const setPositionStatus = api.setPosition(START_FEN);
  const evaluation = api.evaluatePosition();
  const legalMovesCsv = api.getLegalMovesCsv();
  const legalMoves = splitMoves(legalMovesCsv);
  const bestMove = api.searchBestMove(4, 250);

  assert.equal(initStatus, 0, "init_engine should succeed");
  assert.equal(typeof setPositionStatus, "number");
  assert.equal(setPositionStatus, 0, "set_position should accept the starting FEN");
  assert.equal(typeof bestMove, "string");
  assert.match(bestMove, UCI_MOVE_REGEX, "search_best_move should return a UCI move");
  assert.equal(typeof evaluation, "number");
  assert.ok(Number.isFinite(evaluation), "evaluate_position should return a finite score");
  assert.equal(typeof legalMovesCsv, "string");
  assert.equal(legalMoves.length, 20, "get_legal_moves should return 20 legal moves from the start");
});

test("invalid FEN returns an error code and the engine recovers", async () => {
  const { engineModule } = await instantiateEngine();
  const api = createEngineApi(engineModule);

  assert.equal(api.initEngine(), 0, "init_engine should succeed");
  assert.equal(api.setPosition("this is not valid fen"), -1, "invalid FEN should return an error code");
  assert.equal(api.setPosition(START_FEN), 0, "engine should accept a valid FEN after an invalid one");

  const recoveryMove = api.searchBestMove(4, 250);
  assert.match(recoveryMove, UCI_MOVE_REGEX, "engine should still search successfully after recovery");
});

test("sequential searches produce valid UCI moves", async () => {
  const { engineModule } = await instantiateEngine();
  const api = createEngineApi(engineModule);
  const positions = [
    START_FEN,
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "6k1/5ppp/8/8/3P4/4K3/5PPP/8 b - - 0 1",
  ];

  assert.equal(api.initEngine(), 0, "init_engine should succeed");

  for (const fen of positions) {
    assert.equal(api.setPosition(fen), 0, `set_position should accept ${fen}`);
    assert.match(
      api.searchBestMove(4, 250),
      UCI_MOVE_REGEX,
      `search_best_move should return a UCI move for ${fen}`,
    );
  }
});

test("FEN-only set_position flow preserves repetition history", async () => {
  const { engineModule } = await instantiateEngine();
  const api = createEngineApi(engineModule);
  const game = new Chess(REPETITION_START_FEN);

  assert.equal(api.initEngine(), 0, "init_engine should succeed");
  assert.equal(api.setPosition(REPETITION_START_FEN), 0, "initial imbalanced position should be valid");

  for (const move of REPETITION_MOVES) {
    const playedMove = game.move({
      from: move.slice(0, 2),
      to: move.slice(2, 4),
      ...(move.length === 5 ? { promotion: move[4] } : {}),
    });

    assert.ok(playedMove, `move ${move} should be legal in the repetition sequence`);
    assert.equal(api.setPosition(game.fen()), 0, `set_position should accept repetition FEN ${game.fen()}`);
  }

  assert.equal(api.evaluatePosition(), 0, "repetition through set_position should be scored as a draw");
});

test("frontend-style set_position/search flow preserves repetition history", async () => {
  const { engineModule } = await instantiateEngine();
  const api = createEngineApi(engineModule);
  const game = new Chess(FRONTEND_FLOW_START_FEN);

  assert.equal(api.initEngine(), 0, "init_engine should succeed");

  for (let index = 0; index < FRONTEND_FLOW_USER_MOVES.length; index += 1) {
    const userMove = FRONTEND_FLOW_USER_MOVES[index];
    const expectedEngineMove = FRONTEND_FLOW_ENGINE_MOVES[index];
    const playedUserMove = game.move({
      from: userMove.slice(0, 2),
      to: userMove.slice(2, 4),
      ...(userMove.length === 5 ? { promotion: userMove[4] } : {}),
    });

    assert.ok(playedUserMove, `user move ${userMove} should be legal in the frontend flow`);
    assert.equal(api.setPosition(game.fen()), 0, `set_position should accept frontend-flow FEN ${game.fen()}`);

    const bestMove = api.searchBestMove(4, 50);

    assert.equal(bestMove, expectedEngineMove, `search_best_move should follow the expected repetition line at step ${index + 1}`);

    const playedEngineMove = game.move({
      from: bestMove.slice(0, 2),
      to: bestMove.slice(2, 4),
      ...(bestMove.length === 5 ? { promotion: bestMove[4] } : {}),
    });

    assert.ok(playedEngineMove, `engine move ${bestMove} should be legal in the frontend flow`);
  }

  assert.equal(
    api.evaluatePosition(),
    0,
    "frontend-style repetition should be scored as a draw after search_best_move advances the engine position",
  );
});

test("castling and promotion moves use UCI format", async () => {
  const { engineModule } = await instantiateEngine();
  const api = createEngineApi(engineModule);

  assert.equal(api.initEngine(), 0, "init_engine should succeed");
  assert.equal(api.setPosition(CASTLING_FEN), 0, "castling position should be valid");

  const castlingMoves = splitMoves(api.getLegalMovesCsv());
  assert.ok(castlingMoves.includes("e1g1"), "white king-side castling should be encoded as e1g1");
  assert.ok(
    castlingMoves.every((move) => /^[a-h][1-8][a-h][1-8]$/.test(move)),
    "all non-promotion moves should use four-character UCI format",
  );

  assert.equal(api.setPosition(PROMOTION_FEN), 0, "promotion position should be valid");
  const promotionMoves = splitMoves(api.getLegalMovesCsv());

  assert.ok(promotionMoves.includes("e7e8q"), "queen promotion should include the q suffix");
  assert.ok(promotionMoves.includes("e7e8r"), "rook promotion should include the r suffix");
  assert.ok(promotionMoves.includes("e7e8b"), "bishop promotion should include the b suffix");
  assert.ok(promotionMoves.includes("e7e8n"), "knight promotion should include the n suffix");
  assert.ok(
    promotionMoves.every((move) => UCI_MOVE_REGEX.test(move)),
    "promotion moves should still match the UCI move regex",
  );
});

test("WASM memory stays within 2x after 100 depth-4 searches", async () => {
  const { engineModule, memory } = await instantiateEngine({ captureMemory: true });
  const api = createEngineApi(engineModule);

  assert.equal(api.initEngine(), 0, "init_engine should succeed");
  assert.ok(memory instanceof WebAssembly.Memory, "memory export should be captured for leak checks");
  assert.equal(api.setPosition(MEMORY_TEST_FEN), 0, "memory-test position should be valid");

  const firstMove = api.searchBestMove(4, 250);
  const initialMemoryBytes = memory.buffer.byteLength;

  assert.match(firstMove, UCI_MOVE_REGEX, "first search should return a valid UCI move");

  for (let searchIndex = 1; searchIndex < 100; searchIndex += 1) {
    assert.equal(api.setPosition(MEMORY_TEST_FEN), 0, "memory-test position should remain valid");
    assert.match(
      api.searchBestMove(4, 250),
      UCI_MOVE_REGEX,
      `search ${searchIndex + 1} should return a valid UCI move`,
    );
  }

  const finalMemoryBytes = memory.buffer.byteLength;
  assert.ok(
    finalMemoryBytes <= initialMemoryBytes * 2,
    `expected memory growth within 2x, but grew from ${initialMemoryBytes} to ${finalMemoryBytes} bytes`,
  );
});
