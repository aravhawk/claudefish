import assert from "node:assert/strict";
import { createRequire } from "node:module";
import test from "node:test";
import { readFile, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

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

test("engine module initializes successfully", async () => {
  const wasmBuffer = await readFile(engineWasmPath);
  const originalProcess = globalThis.process;
  const originalWindow = globalThis.window;
  const originalDocument = globalThis.document;

  globalThis.process = undefined;
  globalThis.window = globalThis;
  globalThis.document = {
    currentScript: {
      src: `file://${engineJsPath}`,
    },
  };

  try {
    const createChessEngine = require(engineJsPath);
    const engineModule = await createChessEngine({
      locateFile: () => engineWasmPath,
      print: () => {},
      printErr: () => {},
      wasmBinary: wasmBuffer,
    });

    assert.equal(typeof engineModule._init_engine, "function", "engine module should expose _init_engine");
    assert.equal(engineModule._init_engine(), 0, "init_engine should return success");
  } finally {
    globalThis.process = originalProcess;
    globalThis.window = originalWindow;
    globalThis.document = originalDocument;
  }
});
