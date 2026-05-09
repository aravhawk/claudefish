/// <reference lib="webworker" />

import * as Comlink from "comlink";

import type { EngineService } from "@/types/engine";

type EmscriptenArgType = "array" | "boolean" | "number" | "string";
type EmscriptenReturnType = "number" | "string" | null;

interface EmscriptenModule {
  cwrap<TArgs extends readonly unknown[], TResult>(
    ident: string,
    returnType: EmscriptenReturnType,
    argTypes: readonly EmscriptenArgType[],
  ): (...args: TArgs) => TResult;
}

interface CreateChessEngineOptions {
  locateFile(path: string, prefix: string): string;
  mainScriptUrlOrBlob?: string;
}

type CreateChessEngineModule = (
  options: CreateChessEngineOptions,
) => Promise<EmscriptenModule>;

declare const self: DedicatedWorkerGlobalScope & {
  createChessEngine?: CreateChessEngineModule;
};

interface EngineBindings {
  initEngine(): number;
  setPosition(fen: string): number;
  searchBestMove(depth: number, timeMs: number): string;
  evaluatePosition(): number;
  getLegalMovesCsv(): string;
  searchSetThreads(n: number): void;
  resetGame(): void;
}

let bindingsPromise: Promise<EngineBindings> | null = null;
let initializationPromise: Promise<void> | null = null;
let isReady = false;
let operationQueue: Promise<void> = Promise.resolve();

function queueOperation<TResult>(operation: () => Promise<TResult>): Promise<TResult> {
  const nextOperation = operationQueue.then(operation, operation);
  operationQueue = nextOperation.then(
    () => undefined,
    () => undefined,
  );
  return nextOperation;
}

async function loadModule(): Promise<EmscriptenModule> {
  if (typeof self.createChessEngine !== "function") {
    self.importScripts("/engine/engine.js");
  }

  const createChessEngine = self.createChessEngine as CreateChessEngineModule | undefined;

  if (typeof createChessEngine !== "function") {
    throw new Error("createChessEngine was not loaded into the worker global scope.");
  }

  const INIT_TIMEOUT_MS = 120_000;

  const timeoutPromise = new Promise<never>((_, reject) => {
    setTimeout(
      () =>
        reject(
          new Error(
            "Engine timed out after 2 minutes. Check your connection and refresh the page.",
          ),
        ),
      INIT_TIMEOUT_MS,
    );
  });

  return Promise.race([
    createChessEngine({
      locateFile(path) {
        return `/engine/${path}`;
      },
      // Emscripten uses document.currentScript.src to locate the worker script for
      // pthreads, but document is undefined inside a Web Worker. Supplying the URL
      // explicitly prevents new Worker(undefined) and the resulting hang.
      mainScriptUrlOrBlob: "/engine/engine.js",
    }),
    timeoutPromise,
  ]);
}

function createBindings(module: EmscriptenModule): EngineBindings {
  return {
    initEngine: module.cwrap<[], number>("init_engine", "number", []),
    setPosition: module.cwrap<[string], number>("set_position", "number", ["string"]),
    searchBestMove: module.cwrap<[number, number], string>("search_best_move", "string", [
      "number",
      "number",
    ]),
    evaluatePosition: module.cwrap<[], number>("evaluate_position", "number", []),
    getLegalMovesCsv: module.cwrap<[], string>("get_legal_moves", "string", []),
    searchSetThreads: module.cwrap<[number], null>("search_set_threads", null, ["number"]),
    resetGame: module.cwrap<[], null>("reset_game", null, []),
  };
}

async function ensureBindings(): Promise<EngineBindings> {
  if (bindingsPromise === null) {
    bindingsPromise = loadModule()
      .then((module) => createBindings(module))
      .catch((error: unknown) => {
        bindingsPromise = null;
        throw error;
      });
  }

  return bindingsPromise;
}

async function ensureInitialized(): Promise<EngineBindings> {
  const bindings = await ensureBindings();

  if (isReady) {
    return bindings;
  }

  if (initializationPromise === null) {
    initializationPromise = (async () => {
      const status = bindings.initEngine();
      if (status !== 0) {
        throw new Error(`init_engine failed with status ${status}`);
      }
      const threads = Math.min(navigator.hardwareConcurrency ?? 1, 4);
      bindings.searchSetThreads(threads);
      isReady = true;
    })().catch((error: unknown) => {
      isReady = false;
      initializationPromise = null;
      throw error;
    });
  }

  await initializationPromise;
  return bindings;
}

function parseLegalMoves(movesCsv: string): string[] {
  if (movesCsv.length === 0) {
    return [];
  }

  return movesCsv.split(",").filter((move) => move.length > 0);
}

const engineService: EngineService = {
  async initEngine() {
    return queueOperation(async () => {
      await ensureInitialized();
      return 0;
    });
  },

  async isReady() {
    return isReady;
  },

  async setPosition(fen: string) {
    return queueOperation(async () => {
      const bindings = await ensureInitialized();
      return bindings.setPosition(fen);
    });
  },

  async searchBestMove(depth: number, timeMs: number) {
    return queueOperation(async () => {
      const bindings = await ensureInitialized();
      return bindings.searchBestMove(depth, timeMs);
    });
  },

  async evaluatePosition() {
    return queueOperation(async () => {
      const bindings = await ensureInitialized();
      return bindings.evaluatePosition();
    });
  },

  async getLegalMoves() {
    return queueOperation(async () => {
      const bindings = await ensureInitialized();
      return parseLegalMoves(bindings.getLegalMovesCsv());
    });
  },

  async resetGame() {
    return queueOperation(async () => {
      const bindings = await ensureInitialized();
      bindings.resetGame();
    });
  },
};

Comlink.expose(engineService);
