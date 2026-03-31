"use client";

import * as Comlink from "comlink";
import { useCallback, useEffect, useRef, useState } from "react";

import type { EngineService, UseChessEngineResult } from "@/types/engine";

const ENGINE_NOT_READY_ERROR = "Chess engine worker is not ready yet.";
const INVALID_FEN_ERROR = "Failed to set engine position from FEN.";

export function useChessEngine(): UseChessEngineResult {
  const workerRef = useRef<Worker | null>(null);
  const engineRef = useRef<Comlink.Remote<EngineService> | null>(null);
  const mountedRef = useRef(false);
  const [isReady, setIsReady] = useState(false);
  const [isThinking, setIsThinking] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    mountedRef.current = true;

    const worker = new Worker(
      new URL("../workers/chess-engine.worker.ts", import.meta.url),
      { type: "module" },
    );
    const engine = Comlink.wrap<EngineService>(worker);

    workerRef.current = worker;
    engineRef.current = engine;

    void (async () => {
      try {
        setError(null);
        const status = await engine.initEngine();

        if (!mountedRef.current) {
          return;
        }

        if (status !== 0) {
          throw new Error(`init_engine failed with status ${status}`);
        }

        setIsReady(true);
      } catch (initializationError: unknown) {
        if (!mountedRef.current) {
          return;
        }

        setIsReady(false);
        setError(
          initializationError instanceof Error
            ? initializationError.message
            : "Failed to initialize the chess engine worker.",
        );
      }
    })();

    return () => {
      mountedRef.current = false;
      setIsReady(false);
      setIsThinking(false);

      const currentEngine = engineRef.current;
      if (currentEngine !== null) {
        void currentEngine[Comlink.releaseProxy]();
      }

      worker.terminate();
      workerRef.current = null;
      engineRef.current = null;
    };
  }, []);

  const requireEngine = useCallback(() => {
    if (engineRef.current === null || workerRef.current === null || !isReady) {
      throw new Error(ENGINE_NOT_READY_ERROR);
    }

    return engineRef.current;
  }, [isReady]);

  const searchBestMove = useCallback<UseChessEngineResult["searchBestMove"]>(
    async (fen, depth, timeMs) => {
      const engine = requireEngine();

      if (mountedRef.current) {
        setIsThinking(true);
        setError(null);
      }

      try {
        const setPositionStatus = await engine.setPosition(fen);
        if (setPositionStatus !== 0) {
          throw new Error(INVALID_FEN_ERROR);
        }

        return await engine.searchBestMove(depth, timeMs);
      } catch (searchError: unknown) {
        const message =
          searchError instanceof Error ? searchError.message : "Chess engine search failed.";

        if (mountedRef.current) {
          setError(message);
        }

        throw new Error(message);
      } finally {
        if (mountedRef.current) {
          setIsThinking(false);
        }
      }
    },
    [requireEngine],
  );

  const evaluatePosition = useCallback<UseChessEngineResult["evaluatePosition"]>(
    async (fen) => {
      const engine = requireEngine();

      if (mountedRef.current) {
        setError(null);
      }

      const setPositionStatus = await engine.setPosition(fen);
      if (setPositionStatus !== 0) {
        const evaluationError = new Error(INVALID_FEN_ERROR);

        if (mountedRef.current) {
          setError(evaluationError.message);
        }

        throw evaluationError;
      }

      try {
        return await engine.evaluatePosition();
      } catch (evaluationError: unknown) {
        const message =
          evaluationError instanceof Error
            ? evaluationError.message
            : "Position evaluation failed.";

        if (mountedRef.current) {
          setError(message);
        }

        throw new Error(message);
      }
    },
    [requireEngine],
  );

  return {
    isReady,
    isThinking,
    error,
    searchBestMove,
    evaluatePosition,
  };
}
