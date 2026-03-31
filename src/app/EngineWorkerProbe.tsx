"use client";

import { useEffect, useRef, useState } from "react";

import { useChessEngine } from "@/hooks/useChessEngine";

const START_FEN = "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

function formatEvaluation(score: number): string {
  return `${score >= 0 ? "+" : ""}${(score / 100).toFixed(1)}`;
}

export default function EngineWorkerProbe() {
  const { error, evaluatePosition, isReady, isThinking, searchBestMove } = useChessEngine();
  const [bestMove, setBestMove] = useState<string | null>(null);
  const [evaluation, setEvaluation] = useState<number | null>(null);
  const probeStartedRef = useRef(false);

  useEffect(() => {
    let cancelled = false;

    if (!isReady || probeStartedRef.current) {
      return () => {
        cancelled = true;
      };
    }

    probeStartedRef.current = true;

    void (async () => {
      try {
        const score = await evaluatePosition(START_FEN);
        const move = await searchBestMove(START_FEN, 2, 100);

        if (!cancelled) {
          setEvaluation(score);
          setBestMove(move);
        }
      } catch {
        if (!cancelled) {
          setEvaluation(null);
          setBestMove(null);
        }
      }
    })();

    return () => {
      cancelled = true;
    };
  }, [evaluatePosition, isReady, searchBestMove]);

  let status = "Engine worker status: loading…";
  if (error !== null) {
    status = `Engine worker status: ${error}`;
  } else if (isReady && (evaluation === null || bestMove === null || isThinking)) {
    status = "Engine worker status: ready — running startup probe…";
  } else if (isReady && evaluation !== null && bestMove !== null) {
    status = `Engine worker status: ready · eval ${formatEvaluation(evaluation)} · best move ${bestMove}`;
  }

  return (
    <p className="lede" data-testid="engine-worker-status">
      {status}
    </p>
  );
}
